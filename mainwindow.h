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
    uint8_t checkSum(uint8_t * buf ,uint32_t len);
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
    uint16_t header;
    uint8_t address;
    uint8_t cmd;
    uint8_t dataLen;
    union {
        uint8_t v[3];
        unsigned int version:24;
    };
    uint32_t pckSize;
    uint8_t checksum;
} cmdStart;

typedef struct{
    uint16_t header;
    uint8_t cmd;
    uint16_t id;
    uint16_t crc16;
} cmdSend;

typedef struct{
    uint16_t header;
    uint8_t address;
    uint8_t cmd;
    uint8_t dataLen;
    uint32_t crc;
    uint8_t checksum;
} cmdFinish;

typedef enum {
    START_CMD = 0X1A,
    SEND_CMD = 0X1B,
    FINISH_CMD = 0X1C,
} cmd;

#pragma pack ()
#endif // MAINWINDOW_H
