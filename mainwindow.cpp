#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include <stdlib.h>
using namespace std;

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

unsigned short crc16_ccitt(const QByteArray &data)
{
    unsigned short crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<unsigned char>(data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

int MainWindow::tansferData(unsigned short pckIdx)
{
#define SZ_OVER_HEAD 5
#define SZ_CMD 1
#define HEADER 0X55AA
#define OFFSET_CMD 2
#define OFFSET_IDX 3
#define OFFSET_DATA 5

    transNum = (fileLen % pckSize) ? (fileLen + pckSize) / pckSize : fileLen / pckSize;
    char *buf = (char *)malloc(pckSize + SZ_OVER_HEAD);
    unsigned short lastPckSize = fileLen % pckSize ? fileLen % pckSize : pckSize;

    *(unsigned short *)buf = HEADER; 					// Header 2 Bytes
    *(buf + OFFSET_CMD) = SEND_CMD; 					// command of send data 1 Byte
    *(unsigned short *)(buf + OFFSET_IDX) = pckIdx; 	// index 2 Bytes
    memcpy(buf + OFFSET_DATA, firmwareData.data() + (pckSize * (pckIdx - 1)), pckSize);
    serial.write(buf, pckSize + SZ_OVER_HEAD); 			// pckSize + overhead(3)

    /* last data package */
    if (pckIdx == transNum) {
        *(unsigned short *)(buf + OFFSET_IDX) = transNum;
        memcpy(buf + OFFSET_DATA, firmwareData.data() + (pckSize * (transNum - 1)), lastPckSize);
        serial.write(buf, lastPckSize + SZ_OVER_HEAD);	// pckSize + overhead(3)
    }
    free(buf);
}

void MainWindow::readCom()
{
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

    if (temp.at(0) == START_CMD) {
        if (temp.at(1) == 0x0) {
            pckSize = *(unsigned short *)(temp.data() + 2);
            qDebug() << "Negotiated package size:" << (int)temp[2] << endl;
            connect(this, SIGNAL(sendDataSig(unsigned short)), this, SLOT(tansferData(unsigned short)));
            emit sendDataSig(currentPckIdx); 			// send first data package
            qDebug() << "Send fist data package" << endl;
            return;
        } else {
            qDebug() << "This version of firmware cannot be upgrade" << endl;
            return;
        }
    } else if (temp.at(0) == SEND_CMD) {
        currentPckIdx = *(unsigned short *)(temp.data() + SZ_CMD);
        if (currentPckIdx > 0) {
            emit sendDataSig(++currentPckIdx);
            qDebug() << "Send" << currentPckIdx << "th package" << endl;

            /* If the last package trans success */
            if (currentPckIdx == transNum) {
                // trans 3th cmd
                cmdFinish finishCmd;
                crc16 = crc16_ccitt(firmwareData);
                finishCmd.header = HEADER;
                finishCmd.cmd = FINISH_CMD;
                finishCmd.checkSum = crc16;
                serial.write((char *)&finishCmd, sizeof(cmdFinish));
                qDebug() << "Finish cmd has been transed" << endl;
            }
            return;
        } else {
            qDebug() << "Tans" << currentPckIdx << "th failed" << endl;
            return;
        }
    } else if (temp.at(0) == FINISH_CMD) {
        if(temp.at(1) == 0){
            qDebug() << "Upgrade successfully" << endl;
        }else{
            qDebug() << "Upgrade failed" << endl;
        }
    } else {
        qDebug() << "Recived garbage cmd:" << temp.at(0) << endl;
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
    startCmd.header = 0x55AA; // 将帧头0xAA55字节顺序转换
    startCmd.cmd = 0x1A;
    startCmd.version = 0x1;
    startCmd.pkgSize = fileLen;
    //55 AA 1A 01 00 00 00 B4 78 00 00
    //55 AA 1A

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
