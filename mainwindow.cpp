#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <string>
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
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

void MainWindow::readCom()
{
    QByteArray temp = serial.readAll();
    if (temp.at(0) == 0x1A) {
        if (temp.at(1) == 0x0)
        {

        qDebug() << "it's ok to upgrade, pkg size:" << temp[2] << endl;
        qDebug() << (int)temp[2] << endl;
        // 发送第二条命令
    } else if (temp.at(0) == 0x1B) {
        // 如果成功发送第三条命令
    } else if (temp.at(0) == 0x1C) {
    } else {
        qDebug() << "garbage cmd:" << temp[0] << endl;
    }
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
    //int numOfInput = serial.write(ui->uartSendText->toPlainText().toLatin1());   // 以ASCII码的形式通过串口发送出去
    //string statusShow = "the number of input is" + to_string(numOfInput);

    //ui->statusBar->showMessage(QString::fromLocal8Bit(statusShow.c_str()), 2000);
    //QString tmp = "s";
    //QByteArray data();
    //int numOfInput = serial.write(tmp.toLatin1());   // 以ASCII码的形式通过串口发送出去
    /* Open firmware file */
    QString filePath = ui->documentPath->text();
    if(filePath.isEmpty()){
        QMessageBox::warning(this,tr("警告"),tr("未选择文件！"));
        return;
    }

    QFile firmwareFile;
    firmwareFile.setFileName(filePath);
    if(!firmwareFile.open(QIODevice::ReadOnly)){
        QMessageBox::warning(this,tr("警告"),tr("无法打开固件文件！"));
        return;
    }

    /* Read data from firmware */
    QByteArray firmwareData = firmwareFile.readAll();
    firmwareFile.close();
    if(firmwareData.isEmpty()){
        QMessageBox::warning(this,tr("警告"),tr("固件文件为空或者读取失败"));
    }
    qDebug() << "Firmare file loaded. size:" << firmwareData.size() << endl;
    cmdStart startCmd;
    startCmd.header = 0x55AA; // 将帧头0xAA55字节顺序转换
    startCmd.cmd = 0x1A;
    startCmd.version = 0x00000001;
    startCmd.pkgSize = firmwareData.size();
    //55 AA 1A 01 00 00 00 B4 78 00 00
    //55 AA 1A

    //for (int i = 0; i < sizeof(cmdStart); i++) {
    //    serial.write((char *)((&startCmd) + 1), 1);   // 以ASCII码的形式通过串口发送出去
    //}
    serial.write((char *)(&startCmd), 11);   // 以ASCII码的形式通过串口发送出去

    qDebug() << hex;
    qDebug() << "data size:" << sizeof(cmdStart);
    //qDebug() << "QByteArray cmdStart:" << cmdStart;
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
