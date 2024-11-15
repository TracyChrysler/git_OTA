#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include <stdlib.h>
#include <unistd.h>
#include <QtEndian>
#include <string.h>
#include <QTimer>

#define CMD_OFFSET 3
#define DATA_OFFSET 5
#define SZ_OVERHEAD 8
#define HEADER 0X55AA
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
    maxStart = 0;
    maxSend = 1;
    maxFinish = 1;
    startTimer = new QTimer(this);
    sendTimer = new QTimer(this);
    finishTimer = new QTimer(this);
    connect(startTimer, &QTimer::timeout, this, &MainWindow::on_uartSendBtn_clicked);
    connect(finishTimer, &QTimer::timeout, this, &MainWindow::finishCmdSend);
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

void MainWindow::finishCmdSend()
{
    maxFinish ++;
    if(maxFinish == 80){
        ui->uartRecvText->insertPlainText("finish update failed");
        ui->uartRecvText->insertPlainText("\n");
        return;
    }
    cmdFinish finishCmd;
    qDebug() << "checkSum is:" << crc16 << endl;
    finishCmd.header = HEADER;
    finishCmd.address = ADDRESS;
    finishCmd.cmd = FINISH_CMD;
    finishCmd.dataLen = 4;
    uint16_t crc = calculate_crc16_ccitt(firmwareData.data(), fileLen);
    finishCmd.crc = (crc>> 24) & 0xFF;      // 高字节
    finishCmd.crc |= ((crc >> 16) & 0xFF) << 8;  // 第二字节
    finishCmd.crc |= ((crc >> 8) & 0xFF) << 16; // 第三字节
    finishCmd.crc |= (crc & 0xFF) << 24;        // 低字节
    finishCmd.checksum = checkSum((uint8_t *)(&finishCmd + ADDRESS_OFFSET), sizeof(cmdFinish) - 3);
    serial.write((char *)&finishCmd, sizeof(cmdFinish));
    qDebug() << "Finish cmd has been transed" << endl;
}

int MainWindow::tansferData(unsigned short pckIdx)
{
    char *buf = (char *)malloc(pckSize + SZ_OVERHEAD);
    *(unsigned short *)buf = HEADER;
    *(buf + ADDRESS_OFFSET) = ADDRESS;
    *(buf + CMD_OFFSET) = SEND_CMD;
    *(buf + DATA_OFFSET) = (pckIdx >> 8) & 0xFF;
    *(buf + DATA_OFFSET) = (pckIdx << 8) & 0xFF00;

    /* if last data package */
    if (pckIdx == transNum) {
        *(buf + DATALEN_OFFSET) = lastPckSize + SIZE_CURRENTINDEX;
        memcpy(buf + DATA_OFFSET + SIZE_CURRENTINDEX, firmwareData.data() + (pckSize * (pckIdx - 1)), lastPckSize);
        *(buf + DATA_OFFSET + SIZE_CURRENTINDEX + lastPckSize) = checkSum((uint8_t *)buf + ADDRESS_OFFSET, lastPckSize + SIZE_CURRENTINDEX + 3);
        serial.write(buf, lastPckSize + SZ_OVERHEAD);	// pckSize + overhead(3)
        qDebug() << "Send the last package" << endl;
        free(buf);
        return 0;
    }

    *(buf + DATALEN_OFFSET) = pckSize + SIZE_CURRENTINDEX;
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
            startTimer->stop();
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
                finishCmd.header = HEADER;
                finishCmd.address = ADDRESS;
                finishCmd.cmd = FINISH_CMD;
                finishCmd.dataLen = 4;
                uint16_t crc = calculate_crc16_ccitt(firmwareData.data(), fileLen);
                finishCmd.crc = (crc>> 24) & 0xFF;      // 高字节
                finishCmd.crc |= ((crc >> 16) & 0xFF) << 8;  // 第二字节
                finishCmd.crc |= ((crc >> 8) & 0xFF) << 16; // 第三字节
                finishCmd.crc |= (crc & 0xFF) << 24;        // 低字节
                finishCmd.checksum = checkSum((uint8_t *)(&finishCmd + ADDRESS_OFFSET), sizeof(cmdFinish) - 3);
                serial.write((char *)&finishCmd, sizeof(cmdFinish));
                qDebug() << "Finish cmd has been transed" << endl;
                finishTimer->start(3000);
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
            finishTimer->stop();
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

    maxStart ++;
    if (maxStart == 80) {
        ui->uartRecvText->insertPlainText("start update failed");
        ui->uartRecvText->insertPlainText("\n");
        return;
    }

    cmdStart startCmd;
    startCmd.header = HEADER;  // 转换大端
    startCmd.address = ADDRESS;
    startCmd.cmd = START_CMD;
    startCmd.dataLen = 7;
    // 5. version (uint32_t -> 3字节位域)
    unsigned int version = 1;
    startCmd.v[0] = (version >> 16) & 0xFF;  // 高字节
    startCmd.v[1] = (version >> 8) & 0xFF;   // 中字节
    startCmd.v[2] = version & 0xFF;          // 低字节
    startCmd.pckSize = (fileLen>> 24) & 0xFF;      // 高字节
    startCmd.pckSize |= ((fileLen >> 16) & 0xFF) << 8;  // 第二字节
    startCmd.pckSize |= ((fileLen >> 8) & 0xFF) << 16; // 第三字节
    startCmd.pckSize |= (fileLen & 0xFF) << 24;        // 低字节
    startCmd.checksum = checkSum((uint8_t *)(&startCmd) + 2, 10);

    //uint8_t buf[7] = {1, 2, 3, 4, 5, 6, 7};
    //checkSum(buf, sizeof(buf));
    // 将整个结构体转为 QByteArray
    QByteArray byteArray(reinterpret_cast<char*>(&startCmd), sizeof(startCmd));

    // 打印每个字节
    for (int i = 0; i < byteArray.size(); ++i) {
        qDebug() << "Byte " << i << ": " << QString::number((unsigned char)byteArray[i], 16).toUpper();
    }

    qint64 bytesWritten = serial.write((char *)(&startCmd), sizeof(startCmd));

    if (bytesWritten == sizeof(startCmd)) {
        // 成功：所有字节都已写入
        ui->uartRecvText->insertPlainText("send successful");
        ui->uartRecvText->insertPlainText("\n");
        qDebug() << "数据发送成功。";
        startTimer->start(1500);
    } else {
        // 失败：没有发送完整个字节
        ui->uartRecvText->insertPlainText("send failed");
        ui->uartRecvText->insertPlainText("\n");
        qDebug() << "数据发送失败，已发送的字节数：" << bytesWritten;
    }
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
