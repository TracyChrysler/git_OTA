#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QFile>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void readCom();
    void on_uartOpenCloseBtn_clicked();

    void on_uartSendBtn_clicked();

    void on_recvClearBtn_clicked();

    void on_hexStringButton_clicked();

    void on_browseButton_clicked();

    void on_documentPath_editingFinished();

    int tansferData(unsigned short pckIdx);
    //void on_uartRecvText_copyAvailable(bool b);

signals:
    void sendDataSig(unsigned short pckIdx);

private:
    Ui::MainWindow *ui;
    QSerialPort serial;

    void initPort();
    QFile firmwareFile;
    QByteArray firmwareData;
    bool transferComplete;
    unsigned short pckSize;
    unsigned int fileLen;
    unsigned short transNum; // 传输次数
    unsigned short currentPckIdx;
    unsigned short crc16;
    unsigned short lastPckSize;
};

#pragma  pack(1)
typedef struct {
    unsigned short header;
    unsigned char cmd;
    unsigned int version;
    unsigned int pkgSize;
} cmdStart;

typedef struct{
    unsigned short header;
    unsigned char cmd;
    unsigned short id;
    unsigned short crc16;
} cmdSend;

typedef struct{
    unsigned short header;
    unsigned char cmd;
    unsigned int checkSum;
} cmdFinish;

typedef enum {
    START_CMD = 0X1A,
    SEND_CMD = 0X1B,
    FINISH_CMD = 0X1C,
} cmd;

#pragma pack ()
#endif // MAINWINDOW_H
