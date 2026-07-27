#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 手动连接按钮点击信号（推荐方式，比自动连接更清晰可控）
    connect(ui->pushButton, &QPushButton::clicked,
            this, &MainWindow::onPushButtonClicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onPushButtonClicked()
{
    ui->label->setText("Hello World!");
}
