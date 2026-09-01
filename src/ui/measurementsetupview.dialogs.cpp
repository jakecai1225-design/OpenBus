#include "measurementsetupview.h"
#include "ui/thememanager.h"
#include <QFileDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include <QSpinBox>
#include <QComboBox>
#include <QRadioButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QDebug>

// ============================================================
//  File Playback Configuration Dialog — 文件回放配置对话框
// ============================================================

void MeasurementSetupView::showFileConfigDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("File Playback Configuration");
    dlg.setMinimumWidth(420);
    auto *lay = new QVBoxLayout(&dlg);
    
    // Current file
    auto *curLabel = new QLabel(
        m_filePath.isEmpty() ? QStringLiteral("当前未选择文件")
                               : QStringLiteral("当前：%1").arg(m_filePath),
        &dlg);
    curLabel->setWordWrap(true);
    lay->addWidget(curLabel);
    
    // File list
    auto *fileHint = new QLabel("最近文件:", &dlg);
    lay->addWidget(fileHint);
    auto *fileList = new QListWidget(&dlg);
    fileList->setMinimumHeight(120);
    fileList->setMaximumHeight(180);
    for (const auto &f : m_recentFiles)
        fileList->addItem(f);
    if (m_recentFiles.isEmpty())
        fileList->addItem("（暂无最近文件，请点击“浏览...”选择）");
    lay->addWidget(fileList);
    
    // Browse button
    auto *browseBtn = new QPushButton(" 浏览其他文件...", &dlg);
    lay->addWidget(browseBtn);
    
    // Browse file
    QObject::connect(browseBtn, &QPushButton::clicked, this, [this, &dlg]() {
        emit fileBrowseRequested();
        dlg.accept();
    });
    
    // Double-click file list
    QObject::connect(fileList, &QListWidget::itemDoubleClicked, this, [this, fileList](QListWidgetItem *) {
        int row = fileList->currentRow();
        if (row >= 0 && row < m_recentFiles.size()) {
            setFilePath(m_recentFiles[row]);
            emit fileBrowseRequested();
        }
    });
    
    // Button group
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, this, [this, fileList, &dlg]() {
        int row = fileList->currentRow();
        if (row >= 0 && row < m_recentFiles.size()) {
            setFilePath(m_recentFiles[row]);
            emit fileBrowseRequested();
        }
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    
    dlg.exec();
}

// ============================================================
//  Filter Rule Configuration Dialog — Filter 块过滤规则配置对话框
// ============================================================

QStringList MeasurementSetupView::filterRules() const
{
    QStringList rules;
    auto it = m_blocks.constFind("filter");
    if (it == m_blocks.constEnd())
        return rules;
    for (const auto &inst : it->instances)
        rules << inst.title;
    return rules;
}

void MeasurementSetupView::clearFilterRules()
{
    auto it = m_blocks.find("filter");
    if (it == m_blocks.end() || it->instances.isEmpty())
        return;
    it->instances.clear();
    rebuildScene();
    emit filterRulesChanged(QStringList());
}

void MeasurementSetupView::showFilterConfigDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Filter 过滤规则配置");
    dlg.setMinimumWidth(440);
    auto *lay = new QVBoxLayout(&dlg);
    
    auto *hint = new QLabel("规则列表（命中任一规则即放行，空列表 = 不过滤）:", &dlg);
    lay->addWidget(hint);
    
    // Rules list (reflects Filter block instance rows in real-time)
    auto *ruleList = new QListWidget(&dlg);
    ruleList->setAlternatingRowColors(true);
    ruleList->setMinimumHeight(140);
    ruleList->setMaximumHeight(200);
    lay->addWidget(ruleList);
    
    // Refresh rules display
    auto refreshList = [this, ruleList]() {
        ruleList->clear();
        for (const auto &r : filterRules())
            ruleList->addItem(r);
    };
    refreshList();
    
    // ---- New rule form ----
    auto *formGroup = new QGroupBox("新建规则", &dlg);
    auto *form = new QFormLayout(formGroup);
    
    // CAN ID range
    auto *idMin = new QSpinBox(formGroup);
    idMin->setRange(0, 0x7FF);
    idMin->setDisplayIntegerBase(16);
    idMin->setPrefix("0x");
    idMin->setValue(0);
    
    auto *idMax = new QSpinBox(formGroup);
    idMax->setRange(0, 0x7FF);
    idMax->setDisplayIntegerBase(16);
    idMax->setPrefix("0x");
    idMax->setValue(0x7FF);
    
    auto *idRangeWidget = new QWidget(formGroup);
    auto *idRangeLay = new QHBoxLayout(idRangeWidget);
    idRangeLay->setContentsMargins(0, 0, 0, 0);
    idRangeLay->addWidget(idMin);
    idRangeLay->addWidget(new QLabel("~", idRangeWidget));
    idRangeLay->addWidget(idMax);
    form->addRow("ID 范围:", idRangeWidget);
    
    // Frame type options
    auto *chkStd   = new QCheckBox("标准帧", formGroup);
    auto *chkExt   = new QCheckBox("扩展帧", formGroup);
    auto *chkFD    = new QCheckBox("CAN FD", formGroup);
    auto *chkRTR   = new QCheckBox("RTR", formGroup);
    chkStd->setChecked(true);
    chkExt->setChecked(true);
    
    auto *typeWidget = new QWidget(formGroup);
    auto *typeLay = new QHBoxLayout(typeWidget);
    typeLay->setContentsMargins(0, 0, 0, 0);
    typeLay->addWidget(chkStd);
    typeLay->addWidget(chkExt);
    typeLay->addWidget(chkFD);
    typeLay->addWidget(chkRTR);
    form->addRow("帧类型:", typeWidget);
    
    // Direction filter
    auto *comboDir = new QComboBox(formGroup);
    comboDir->addItems({"全部", "仅 Tx (发送)", "仅 Rx (接收)"});
    form->addRow("方向:", comboDir);
    
    lay->addWidget(formGroup);
    
    // ---- Add/Remove buttons ----
    auto *btnLay = new QHBoxLayout();
    auto *addBtn = new QPushButton("+ 添加规则", &dlg);
    auto *delBtn = new QPushButton("- 移除选中规则", &dlg);
    btnLay->addWidget(addBtn);
    btnLay->addWidget(delBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);
    
    // Add rule: immediate effect (append to Filter block and refresh scene)
    QObject::connect(addBtn, &QPushButton::clicked, this,
                     [this, idMin, idMax, chkStd, chkExt, chkFD, chkRTR, comboDir, &refreshList]() {
        QStringList types;
        if (chkStd->isChecked()) types << "Std";
        if (chkExt->isChecked()) types << "Ext";
        if (chkFD->isChecked())  types << "FD";
        if (chkRTR->isChecked()) types << "RTR";
        if (types.isEmpty())
            types << "全部";
        
        auto it = m_blocks.find("filter");
        if (it == m_blocks.end())
            return;
        InstanceItem rule;
        rule.id = QStringLiteral("rule%1").arg(it->instances.size() + 1);
        rule.title = QStringLiteral("ID 0x%1~0x%2 · %3 · %4")
                         .arg(idMin->value(), 0, 16)
                         .arg(idMax->value(), 0, 16)
                         .arg(types.join("+"))
                         .arg(comboDir->currentText());
        it->instances.append(rule);
        
        rebuildScene();
        refreshList();
        emit filterRulesChanged(filterRules());
        qDebug() << "Filter rule added:" << rule.title;
    });
    
    // Remove selected rule: immediate effect
    QObject::connect(delBtn, &QPushButton::clicked, this,
                     [this, ruleList, &refreshList]() {
        int row = ruleList->currentRow();
        auto it = m_blocks.find("filter");
        if (row < 0 || it == m_blocks.end() || row >= it->instances.size())
            return;
        const QString removed = it->instances[row].title;
        it->instances.removeAt(row);
        
        rebuildScene();
        refreshList();
        emit filterRulesChanged(filterRules());
        qDebug() << "Filter rule removed:" << removed;
    });
    
    // Close (no OK/Cancel—rule additions/removals take effect immediately)
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    lay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    
    dlg.exec();
}

// ============================================================
//  DBC File Selection Dialog — DBC 文件选择对话框
// ============================================================

void MeasurementSetupView::showDbcSelectDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("选择 DBC 文件");
    dlg.setMinimumWidth(420);
    auto *lay = new QVBoxLayout(&dlg);
    
    auto *hint = new QLabel("当前工程已加载的 DBC 文件:", &dlg);
    lay->addWidget(hint);
    
    auto *list = new QListWidget(&dlg);
    list->setAlternatingRowColors(true);
    list->setMinimumHeight(150);
    for (const auto &name : m_dbcFiles)
        list->addItem(name);
    if (m_dbcFiles.isEmpty())
        list->addItem("（未加载任何 DBC 文件，请先通过侧边栏导入）");
    lay->addWidget(list);
    
    // Import new file button
    auto *importBtn = new QPushButton("导入新 DBC 文件...", &dlg);
    lay->addWidget(importBtn);
    
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    
    // Double-click selection
    connect(list, &QListWidget::itemDoubleClicked, this, [this, list, &dlg](QListWidgetItem *) {
        int row = list->currentRow();
        if (row >= 0 && row < m_dbcFiles.size()) {
            qDebug() << "Selected DBC:" << m_dbcFiles[row];
            emit dbcSelectRequested();
            dlg.accept();
        }
    });
    
    connect(importBtn, &QPushButton::clicked, this, [this, &dlg]() {
        emit dbcSelectRequested();
        dlg.accept();
    });
    
    connect(btns, &QDialogButtonBox::accepted, this, [this, list, &dlg]() {
        int row = list->currentRow();
        if (row >= 0 && row < m_dbcFiles.size()) {
            qDebug() << "Selected DBC:" << m_dbcFiles[row];
        }
        emit dbcSelectRequested();
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    
    dlg.exec();
}
