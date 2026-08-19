#include "settingsdialog.h"
#include "core/appconfig.h"
#include "core/logging.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFont>
#include <QFrame>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("设置");
    setMinimumSize(800, 500);
    setupUi();
    setupMetas();
    populateCategoryTree();
    populateSettingsTree(QString(), QString());
}

void SettingsDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 顶部搜索栏 ----
    auto *searchBar = new QFrame(this);
    searchBar->setObjectName("SettingsSearchBar");
    searchBar->setFrameShape(QFrame::NoFrame);
    auto *searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(8, 6, 8, 6);

    m_searchEdit = new QLineEdit(searchBar);
    m_searchEdit->setPlaceholderText("搜索设置...");
    m_searchEdit->setClearButtonEnabled(true);
    // 原生清除按钮 × 不随主题（深色下不可见）→ 换主题色 SVG 图标
    applyClearButtonIcon(m_searchEdit, ThemeManager::instance()->currentTheme().text);
    auto *searchIcon = new QLabel(searchBar);
    searchIcon->setPixmap(renderSvgPixmap(
        ":/icons/search.svg",
        ThemeManager::instance()->currentTheme().text, 14));
    searchLayout->addWidget(searchIcon);
    searchLayout->addWidget(m_searchEdit, 1);

    // JSON 编辑切换按钮
    auto *jsonBtn = new QPushButton("编辑 JSON", searchBar);
    jsonBtn->setCheckable(true);
    searchLayout->addWidget(jsonBtn);

    root->addWidget(searchBar);

    // ---- 主体: 左分类树 + 右内容栈 ----
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧分类
    m_categoryTree = new QTreeWidget(splitter);
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setMinimumWidth(160);
    m_categoryTree->setMaximumWidth(240);

    // 右侧栈
    m_rightStack = new QStackedWidget(splitter);

    // -- 设置列表页 --
    m_settingsListPage = new QWidget(m_rightStack);
    auto *listLayout = new QVBoxLayout(m_settingsListPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    m_settingsTree = new QTreeWidget(m_settingsListPage);
    m_settingsTree->setColumnCount(2);
    m_settingsTree->setHeaderLabels({"设置项", "值"});
    m_settingsTree->header()->setStretchLastSection(false);
    m_settingsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_settingsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_settingsTree->setRootIsDecorated(false);
    m_settingsTree->setAlternatingRowColors(true);
    listLayout->addWidget(m_settingsTree);

    m_rightStack->addWidget(m_settingsListPage);

    // -- JSON 编辑页 --
    m_jsonPage = new QWidget(m_rightStack);
    auto *jsonLayout = new QVBoxLayout(m_jsonPage);
    jsonLayout->setContentsMargins(4, 4, 4, 4);

    auto *jsonLabel = new QLabel("settings.json", m_jsonPage);
    jsonLabel->setObjectName("SidePanelSubTitle");
    jsonLayout->addWidget(jsonLabel);

    m_jsonEdit = new QPlainTextEdit(m_jsonPage);
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    m_jsonEdit->setFont(mono);
    m_jsonEdit->setPlainText(AppConfig::instance()->toJsonString());
    jsonLayout->addWidget(m_jsonEdit, 1);

    m_rightStack->addWidget(m_jsonPage);

    splitter->addWidget(m_categoryTree);
    splitter->addWidget(m_rightStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({180, 620});

    root->addWidget(splitter, 1);

    // ---- 底部状态栏 + 按钮 ----
    auto *bottomBar = new QFrame(this);
    auto *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(8, 4, 8, 4);

    m_statusLabel = new QLabel(bottomBar);
    m_statusLabel->setObjectName("DimLabel");
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_resetBtn = new QPushButton("重置为默认", bottomBar);
    m_saveBtn = new QPushButton("保存", bottomBar);
    m_saveBtn->setDefault(true);
    bottomLayout->addWidget(m_resetBtn);
    bottomLayout->addWidget(m_saveBtn);

    root->addWidget(bottomBar);

    // ---- 连接 ----
    connect(m_searchEdit, &QLineEdit::textChanged, this, &SettingsDialog::onSearchChanged);
    connect(m_categoryTree, &QTreeWidget::itemClicked, this, &SettingsDialog::onCategorySelected);
    connect(jsonBtn, &QPushButton::toggled, this, [this, jsonBtn](bool checked) {
        if (checked) {
            m_jsonEdit->setPlainText(AppConfig::instance()->toJsonString());
            switchToJsonPage();
            jsonBtn->setText("设置列表");
        } else {
            switchToSettingsPage();
            jsonBtn->setText("编辑 JSON");
        }
    });
    connect(m_jsonEdit, &QPlainTextEdit::textChanged, this, &SettingsDialog::onJsonEdited);
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsDialog::onReset);
}

void SettingsDialog::setupMetas()
{
    // 定义所有设置项的元数据
    m_metas.clear();

    auto add = [this](const QString &key, const QString &label,
                      const QString &category, const QString &type,
                      const QString &desc, const QStringList &choices = {}) {
        SettingMeta m;
        m.key = key;
        m.label = label;
        m.category = category;
        m.type = type;
        m.desc = desc;
        m.comboChoices = choices;
        m_metas.append(m);
    };

    // ---- 通用 ----
    add("font.family", "字体族", "通用", "string", "界面字体名称");
    add("font.size", "字体大小", "通用", "int", "界面字体大小 (px)");
    add("window.rememberGeometry", "记住窗口大小", "通用", "bool", "下次启动恢复上次窗口尺寸");
    add("window.width", "窗口宽度", "通用", "int", "初始窗口宽度");
    add("window.height", "窗口高度", "通用", "int", "初始窗口高度");

    // ---- Trace ----
    add("trace.maxFrames", "最大帧数", "Trace", "int", "缓冲区最大帧数，超过后从头部丢弃");
    add("trace.overwriteMode", "覆盖模式", "Trace", "bool", "同 CAN ID 的帧只保留一行");
    add("trace.autoScroll", "自动滚动", "Trace", "bool", "新帧到达时自动滚动到底部");
    add("trace.showGrid", "显示网格线", "Trace", "bool", "表格中显示网格线");
    add("trace.alternatingRowColors", "交替行颜色", "Trace", "bool", "奇偶行使用不同背景色");

    // ---- Graphic ----
    add("graphic.timeWindow", "时间窗口 (秒)", "Graphic", "double", "波形图显示最近 N 秒数据");
    add("graphic.antialiasing", "抗锯齿", "Graphic", "bool", "波形图启用抗锯齿渲染");
    add("graphic.fps", "刷新率 (FPS)", "Graphic", "int", "波形图刷新帧率");

    // ---- Record ----
    add("record.defaultFormat", "默认录制格式", "Record", "combo", "新录制文件的默认格式", {"openbus", "asc", "blf"});
    add("record.autoSave", "自动保存", "Record", "bool", "停止录制时自动保存文件");

    // ---- 日志 ----
    add("log.level", "日志级别", "日志", "combo", "spdlog 输出级别", {"trace", "debug", "info", "warn", "error", "critical"});
    add("log.maxFileSize", "日志文件最大字节", "日志", "int", "单个日志文件最大字节数");
    add("log.maxFiles", "日志文件最大数量", "日志", "int", "轮转保留的日志文件数");
}

void SettingsDialog::populateCategoryTree()
{
    // 收集去重分类
    QStringList categories;
    for (const auto &m : m_metas) {
        if (!categories.contains(m.category))
            categories << m.category;
    }

    m_categoryTree->clear();
    // "全部" 项
    auto *allItem = new QTreeWidgetItem({"全部设置"});
    allItem->setData(0, Qt::UserRole, QString());
    m_categoryTree->addTopLevelItem(allItem);

    for (const auto &cat : categories) {
        auto *item = new QTreeWidgetItem({cat});
        item->setData(0, Qt::UserRole, cat);
        m_categoryTree->addTopLevelItem(item);
    }

    m_categoryTree->setCurrentItem(allItem);
}

void SettingsDialog::populateSettingsTree(const QString &categoryFilter, const QString &textFilter)
{
    m_settingsTree->clear();

    auto *cfg = AppConfig::instance();

    for (const auto &m : m_metas) {
        // 分类过滤
        if (!categoryFilter.isEmpty() && m.category != categoryFilter)
            continue;
        // 文本过滤
        if (!textFilter.isEmpty()) {
            if (!m.key.contains(textFilter, Qt::CaseInsensitive) &&
                !m.label.contains(textFilter, Qt::CaseInsensitive) &&
                !m.desc.contains(textFilter, Qt::CaseInsensitive))
                continue;
        }

        auto *item = new QTreeWidgetItem();
        item->setText(0, m.label);
        item->setToolTip(0, m.key + "\n" + m.desc);
        item->setData(0, Qt::UserRole, m.key);

        // 根据类型创建编辑器
        QWidget *editor = nullptr;

        if (m.type == "bool") {
            auto *chk = new QCheckBox();
            chk->setChecked(cfg->getBool(m.key, false));
            connect(chk, &QCheckBox::toggled, this, [cfg, key = m.key](bool v) {
                cfg->set(key, v);
            });
            editor = chk;
            item->setText(1, cfg->getBool(m.key, false) ? "true" : "false");
        } else if (m.type == "int") {
            auto *spin = new QSpinBox();
            spin->setRange(0, 9999999);
            spin->setValue(cfg->getInt(m.key, 0));
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [cfg, key = m.key, item](int v) {
                    cfg->set(key, v);
                    item->setText(1, QString::number(v));
                });
            editor = spin;
            item->setText(1, QString::number(cfg->getInt(m.key, 0)));
        } else if (m.type == "double") {
            auto *spin = new QDoubleSpinBox();
            spin->setRange(0.0, 999999.0);
            spin->setDecimals(1);
            spin->setValue(cfg->getDouble(m.key, 0.0));
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [cfg, key = m.key, item](double v) {
                    cfg->set(key, v);
                    item->setText(1, QString::number(v, 'f', 1));
                });
            editor = spin;
            item->setText(1, QString::number(cfg->getDouble(m.key, 0.0), 'f', 1));
        } else if (m.type == "combo") {
            auto *combo = new QComboBox();
            for (const auto &c : m.comboChoices)
                combo->addItem(c);
            QString cur = cfg->getString(m.key);
            int idx = combo->findText(cur);
            if (idx >= 0) combo->setCurrentIndex(idx);
            connect(combo, &QComboBox::currentTextChanged, this,
                [cfg, key = m.key, item](const QString &v) {
                    cfg->set(key, v);
                    item->setText(1, v);
                });
            editor = combo;
            item->setText(1, cur);
        } else { // string
            auto *edit = new QLineEdit();
            edit->setText(cfg->getString(m.key));
            connect(edit, &QLineEdit::textChanged, this,
                [cfg, key = m.key, item](const QString &v) {
                    cfg->set(key, v);
                    item->setText(1, v);
                });
            editor = edit;
            item->setText(1, cfg->getString(m.key));
        }

        if (editor) {
            editor->setToolTip(m.key + "\n" + m.desc);
            m_settingsTree->setItemWidget(item, 1, editor);
        }

        m_settingsTree->addTopLevelItem(item);
    }

    m_statusLabel->setText(QString("共 %1 项设置").arg(m_settingsTree->topLevelItemCount()));
}

void SettingsDialog::onSearchChanged(const QString &text)
{
    // 获取当前分类
    auto *catItem = m_categoryTree->currentItem();
    QString cat = catItem ? catItem->data(0, Qt::UserRole).toString() : QString();
    populateSettingsTree(cat, text);
}

void SettingsDialog::onCategorySelected(QTreeWidgetItem *item)
{
    if (!item) return;
    QString cat = item->data(0, Qt::UserRole).toString();
    populateSettingsTree(cat, m_searchEdit->text());
}

void SettingsDialog::onJsonEdited()
{
    m_statusLabel->setText("JSON 已修改 — 点击保存生效");
}

void SettingsDialog::onSave()
{
    // 如果当前在 JSON 页面，先解析 JSON
    if (m_rightStack->currentIndex() == 1) {
        QString jsonStr = m_jsonEdit->toPlainText();
        if (!AppConfig::instance()->fromJsonString(jsonStr)) {
            m_statusLabel->setText("JSON 解析失败，请检查语法");
            return;
        }
    }
    AppConfig::instance()->save();
    m_statusLabel->setText("已保存");
    spdlog::info("SettingsDialog: 配置已保存");
}

void SettingsDialog::onReset()
{
    AppConfig::instance()->fromJsonString(
        QString::fromStdString(AppConfig::defaultConfig().dump(4)));
    populateSettingsTree(QString(), m_searchEdit->text());
    m_jsonEdit->setPlainText(AppConfig::instance()->toJsonString());
    m_statusLabel->setText("已重置为默认值");
}

void SettingsDialog::switchToJsonPage()
{
    m_rightStack->setCurrentIndex(1);
}

void SettingsDialog::switchToSettingsPage()
{
    m_rightStack->setCurrentIndex(0);
}
