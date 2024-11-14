#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include <stdlib.h>
#include <unistd.h>
#include <QtEndian>

#define CMD_OFFSET 3
#define DATA_OFFSET 5
#define SZ_OVERHEAD 8
#define HEADER 0XAA55
#define ADDRESS_OFFSET 2
#define DATALEN_OFFSET 4
#define ADDRESS 0X15
#define SIZE_CURRENTINDEX 2
using namespace std;

uint32_t qto_data_filed_BigEndian(uint32_t value)
{
     return qToBigEndian(value);
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    initPort();
}

MainWindow::~MainWindow()
{
    delete ui;
}

uint16_t calculate_crc16_ccitt(char *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

int MainWindow::tansferData(unsigned short pckIdx)
{
    char *buf = (char *)malloc(pckSize + SZ_OVERHEAD);
    *(unsigned short *)buf = qToBigEndian(HEADER);
    *(buf + ADDRESS_OFFSET) = ADDRESS;
    *(buf + CMD_OFFSET) = SEND_CMD;
    *(unsigned short *)(buf + DATA_OFFSET) = pckIdx;

    /* if last data package */
    if (pckIdx == transNum) {
        *(buf + DATALEN_OFFSET) = qToBigEndian(lastPckSize + SIZE_CURRENTINDEX);
        memcpy(buf + DATA_OFFSET + SIZE_CURRENTINDEX, firmwareData.data() + (pckSize * (pckIdx - 1)), lastPckSize);
        *(buf + DATA_OFFSET + SIZE_CURRENTINDEX + lastPckSize) = checkSum((uint8_t *)buf + ADDRESS_OFFSET, lastPckSize + SIZE_CURRENTINDEX + 3);
        serial.write(buf, lastPckSize + SZ_OVERHEAD);	// pckSize + overhead(3)
        qDebug() << "Send the last package" << endl;
        free(buf);
        return 0;
    }

    *(buf + DATALEN_OFFSET) = qToBigEndian(pckSize + SIZE_CURRENTINDEX);
    memcpy(buf + DATA_OFFSET + SIZE_CURRENTINDEX, firmwareData.data() + (pckSize * (pckIdx - 1)), pckSize);
    *(buf + DATA_OFFSET + SIZE_CURRENTINDEX + pckSize) = checkSum((uint8_t *)buf + ADDRESS_OFFSET, pckSize + SIZE_CURRENTINDEX + 3);
    serial.write(buf, pckSize + SZ_OVERHEAD); 			// pckSize + overhead(3)
    qDebug() << "Send" << currentPckIdx << "th package" << endl;

    free(buf);
    return 0;
}

void MainWindow::readCom()
{
    //qDebug() << "enter readCom" << endl;
    QByteArray temp = serial.readAll();
    /* Display reived data in textBox */
    if(!temp.isEmpty()){
        if(ui->hexStringButton->text() == "String"){
            ui->uartRecvText->insertPlainText(temp);
            ui->uartRecvText->insertPlainText("\n");
        }else{
            //            temp = serial.readAll();
            ui->uartRecvText->insertPlainText("0x");
            ui->uartRecvText->insertPlainText(temp.toHex());
            ui->uartRecvText->insertPlainText("\n");
        }
    }else{
        qDebug() << "temp is empty" << endl;
    }


    if (temp.at(CMD_OFFSET) == START_CMD) {
        if (temp.at(DATA_OFFSET) == 0x0) {
            pckSize = *(unsigned short *)(temp.data() + DATA_OFFSET + 1);
            // calculate package size
            transNum = (fileLen % pckSize) ? (fileLen + pckSize) / pckSize : fileLen / pckSize;
            lastPckSize = fileLen % pckSize ? fileLen % pckSize : pckSize;
            qDebug() << "the trans num is:" << transNum << endl;
            qDebug() << "last package size:" << lastPckSize << endl;
            qDebug() << "Negotiated package size:" << (int)temp[2] << endl;
            connect(this, SIGNAL(sendDataSig(unsigned short)), this, SLOT(tansferData(unsigned short)));
            emit sendDataSig(currentPckIdx); 			// send first data package
            qDebug() << "Send fist data package" << endl;
            return;
        } else {
            qDebug() << "This version of firmware cannot be upgrade" << endl;
            return;
        }
    } else if (temp.at(CMD_OFFSET) == SEND_CMD) {
        unsigned short id = *(unsigned short *)(temp.data() + DATA_OFFSET);
        if (id <= 0x0) {
           qDebug() << "Checksum incorrect" << endl;
           return;
        }
        qDebug() << "recieved id:" << id << endl;
        currentPckIdx = *(unsigned short *)(temp.data() + DATA_OFFSET);
        if (currentPckIdx > 0) {
            /* If the last package trans success */
            if (currentPckIdx == transNum) {
                // trans 3th cmd
                cmdFinish finishCmd;
                qDebug() << "checkSum is:" << crc16 << endl;
                finishCmd.header = qToBigEndian(HEADER);
                finishCmd.address = ADDRESS;
                finishCmd.cmd = FINISH_CMD;
                finishCmd.crc = calculate_crc16_ccitt(firmwareData.data(), fileLen);
                finishCmd.checksum = checkSum((uint8_t *)(&finishCmd + ADDRESS_OFFSET), sizeof(cmdFinish) - 3);
                serial.write((char *)&finishCmd, sizeof(cmdFinish));
                qDebug() << "Finish cmd has been transed" << endl;
                return;
            }
            emit sendDataSig(++currentPckIdx);

            return;
        } else {
            qDebug() << "Tans" << currentPckIdx << "th failed" << endl;
            return;
        }
    } else if (temp.at(CMD_OFFSET) == FINISH_CMD) {
        if(temp.at(DATA_OFFSET) == 0){
            qDebug() << "Upgrade successfully" << endl;
        }else{
            qDebug() << "Upgrade failed" << endl;
        }
    } else {
        qDebug() << "Recived garbage cmd:" << temp.at(CMD_OFFSET) << endl;
    }
}

void MainWindow::on_uartOpenCloseBtn_clicked()
{
    if(ui->portNameCombo->isEnabled()){
        ui->uartOpenCloseBtn->setText("Close");
        ui->portNameCombo->setDisabled(true);
        serial.setPortName(ui->portNameCombo->currentText());
        serial.setBaudRate(QSerialPort::BaudRate(ui->baudRateCombo->currentText().toInt()), QSerialPort::AllDirections);
        serial.setDataBits(QSerialPort::DataBits(ui->dataBitsCombo->currentText().toInt()));
        serial.setFlowControl(QSerialPort::NoFlowControl);
        serial.setParity(QSerialPort::Parity(ui->parityCombo->currentIndex() == 0 ?
                                 0 : ui->parityCombo->currentIndex() + 1));
        serial.setStopBits(QSerialPort::StopBits(ui->stopBitsComb->currentText().toFloat() == 1.5 ?
                                 3 : ui->stopBitsComb->currentText().toInt()));
        serial.close(); // 先关串口，再打开，可以保证串口不被其他函数占用
        if(serial.open(QIODevice::ReadWrite)){
            qDebug() << "Serial opened" << endl;
            connect(&serial, SIGNAL(readyRead()), this, SLOT(readCom()));   // 把串口的readRead()信号绑定到readCom()这个槽函数上
        }
    }else{
        ui->uartOpenCloseBtn->setText("Open");
        ui->portNameCombo->setEnabled(true);
        serial.close();
    }
}

void MainWindow::on_uartSendBtn_clicked()
{
    /* Open firmware file */
    QString filePath = ui->documentPath->text();
    if(filePath.isEmpty()){
        QMessageBox::warning(this,tr("警告"),tr("未选择文件！"));
        return;
    }

    firmwareFile.setFileName(filePath);
    if(!firmwareFile.open(QIODevice::ReadOnly)){
        QMessageBox::warning(this,tr("警告"),tr("无法打开固件文件！"));
        return;
    }

    /* Read data from firmware */
    firmwareData = firmwareFile.readAll();
    firmwareFile.close();
    if(firmwareData.isEmpty()){
        QMessageBox::warning(this,tr("警告"),tr("固件文件为空或者读取失败"));
    }
    fileLen = firmwareData.size();
    qDebug() << "Firmare file loaded. size:" << fileLen << endl;

    cmdStart startCmd;
    startCmd.header = qToBigEndian(0xAA55); // 将帧头0xAA55字节顺序转换
    startCmd.address = 0x15;
    startCmd.cmd = START_CMD;
    startCmd.dataLen = 7;
    startCmd.version = qto_data_filed_BigEndian(1);
    startCmd.pckSize = qto_data_filed_BigEndian(fileLen);
    startCmd.checksum = checkSum((uint8_t *)(&startCmd + 2), sizeof(cmdStart) - 3);

    serial.write((char *)(&startCmd), sizeof(cmdStart));
    qDebug() << "data size:" << sizeof(cmdStart) << endl;

    currentPckIdx = 1;
}

void MainWindow::initPort()
{
    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts()){
        QSerialPort serialtmp;
        serialtmp.setPort(info);
        /* 判断端口是否能打开 */
        if(serialtmp.open(QIODevice::ReadWrite))
        {
            int isHaveItemInList = 0;
            /* 判断是不是已经在列表中了 */
            for(int i=0; i<ui->portNameCombo->count(); i++)
            {
                /* 如果，已经在列表中了，那么不添加这一项了就 */
                if(ui->portNameCombo->itemData(i) == serialtmp.portName())
                {
                    isHaveItemInList++;
                }
            }

            if(isHaveItemInList == 0)
            {
                ui->portNameCombo->addItem(serialtmp.portName());
            }
            serialtmp.close();
        }
    }
}

void MainWindow::on_recvClearBtn_clicked()
{
    ui->uartRecvText->clear();
}

void MainWindow::on_hexStringButton_clicked()
{
    if(ui->hexStringButton->text() == "String")
        ui->hexStringButton->setText("Hex");
    else
        ui->hexStringButton->setText("String");
}

void MainWindow::on_browseButton_clicked()
{
    qDebug() << "open dialog" << endl;
    QString fileName = QFileDialog::getOpenFileName(this,tr("Open File"),"",tr("All File(*)"));
    if(fileName.isEmpty()){
        //如果未选择文件，弹出警告框
        QMessageBox::warning(this,tr("警告"),tr("未选择文件！"));
    }
    else{
        ui->documentPath->setText(fileName);
    }
}

void MainWindow::on_documentPath_editingFinished()
{
    QString filePath = ui->documentPath->text();
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        QMessageBox::warning(this, tr("警告"), tr("文件路径无效或文件不存在！"));
        return;
    }
}

uint8_t MainWindow::checkSum(uint8_t * buf ,uint32_t len)
{
    uint32_t sum = 0;
    uint32_t i;
    for(i=0;i<len;i++)
    {
        sum+=buf[i];
    }
    qDebug("2222222 The sum : <0x%x>", (uint8_t)sum);
    return (uint8_t)sum;
}
