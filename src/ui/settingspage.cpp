#include "settingspage.h"
#include "core/appconfig.h"
#include "core/logging.h"
#include "core/translationmanager.h"
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
#include <QSignalBlocker>

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setupMetas();
    populateCategoryTree();
    populateSettingsTree(QString(), QString());
    retranslateUi();
}

void SettingsPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *searchBar = new QFrame(this);
    searchBar->setObjectName("SettingsSearchBar");
    searchBar->setFrameShape(QFrame::NoFrame);
    auto *searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(8, 6, 8, 6);

    m_searchEdit = new QLineEdit(searchBar);
    m_searchEdit->setClearButtonEnabled(true);
    applyClearButtonIcon(m_searchEdit, ThemeManager::instance()->currentTheme().text);
    auto *searchIcon = new QLabel(searchBar);
    searchIcon->setPixmap(renderSvgPixmap(
        ":/icons/search.svg",
        ThemeManager::instance()->currentTheme().text, 14));
    searchLayout->addWidget(searchIcon);
    searchLayout->addWidget(m_searchEdit, 1);

    m_jsonBtn = new QPushButton(searchBar);
    m_jsonBtn->setCheckable(true);
    searchLayout->addWidget(m_jsonBtn);

    root->addWidget(searchBar);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_categoryTree = new QTreeWidget(splitter);
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setMinimumWidth(160);
    m_categoryTree->setMaximumWidth(240);

    m_rightStack = new QStackedWidget(splitter);

    m_settingsListPage = new QWidget(m_rightStack);
    auto *listLayout = new QVBoxLayout(m_settingsListPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    m_settingsTree = new QTreeWidget(m_settingsListPage);
    m_settingsTree->setColumnCount(2);
    m_settingsTree->header()->setStretchLastSection(false);
    m_settingsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_settingsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_settingsTree->setRootIsDecorated(false);
    m_settingsTree->setAlternatingRowColors(true);
    listLayout->addWidget(m_settingsTree);

    m_rightStack->addWidget(m_settingsListPage);

    m_jsonPage = new QWidget(m_rightStack);
    auto *jsonLayout = new QVBoxLayout(m_jsonPage);
    jsonLayout->setContentsMargins(4, 4, 4, 4);

    auto *jsonLabel = new QLabel(QStringLiteral("settings.json"), m_jsonPage);
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

    auto *bottomBar = new QFrame(this);
    auto *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(8, 4, 8, 4);

    m_statusLabel = new QLabel(bottomBar);
    m_statusLabel->setObjectName("DimLabel");
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_resetBtn = new QPushButton(bottomBar);
    m_saveBtn = new QPushButton(bottomBar);
    bottomLayout->addWidget(m_resetBtn);
    bottomLayout->addWidget(m_saveBtn);

    root->addWidget(bottomBar);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &SettingsPage::onSearchChanged);
    connect(m_categoryTree, &QTreeWidget::itemClicked, this, &SettingsPage::onCategorySelected);
    connect(m_jsonBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_jsonEdit->setPlainText(AppConfig::instance()->toJsonString());
            switchToJsonPage();
            m_jsonBtn->setText(tr("Settings list"));
        } else {
            switchToSettingsPage();
            m_jsonBtn->setText(tr("Edit JSON"));
        }
    });
    connect(m_jsonEdit, &QPlainTextEdit::textChanged, this, &SettingsPage::onJsonEdited);
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsPage::onSave);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsPage::onReset);
}

void SettingsPage::setupMetas()
{
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

    add(QStringLiteral("ui.language"), tr("Language"), tr("General"),
        QStringLiteral("language"), tr("UI language (applies immediately)"));
    add(QStringLiteral("font.family"), QStringLiteral("Font family"), QStringLiteral("General"),
        QStringLiteral("string"), QStringLiteral("UI font family"));
    add(QStringLiteral("font.size"), QStringLiteral("Font size"), QStringLiteral("General"),
        QStringLiteral("int"), QStringLiteral("UI font size (px)"));
    add(QStringLiteral("window.rememberGeometry"), QStringLiteral("Remember window size"),
        QStringLiteral("General"), QStringLiteral("bool"),
        QStringLiteral("Restore last window size on next launch"));
    add(QStringLiteral("window.width"), QStringLiteral("Window width"), QStringLiteral("General"),
        QStringLiteral("int"), QStringLiteral("Initial window width"));
    add(QStringLiteral("window.height"), QStringLiteral("Window height"), QStringLiteral("General"),
        QStringLiteral("int"), QStringLiteral("Initial window height"));

    add(QStringLiteral("trace.maxFrames"), QStringLiteral("Max frames (local ring)"),
        QStringLiteral("Trace"), QStringLiteral("int"),
        QStringLiteral("Local Trace ring capacity for offline/overwrite (live uses CaptureLog)"));
    add(QStringLiteral("trace.cacheRows"), QStringLiteral("Viewport cache rows"),
        QStringLiteral("Trace"), QStringLiteral("int"),
        QStringLiteral("QTableView window size (T3); display cost ≈ this many rows"));
    add(QStringLiteral("trace.visibleRows"), QStringLiteral("Visible rows hint"),
        QStringLiteral("Trace"), QStringLiteral("int"),
        QStringLiteral("Typical on-screen rows; cacheRows should be ~2x this"));
    add(QStringLiteral("trace.overwriteMode"), QStringLiteral("Overwrite mode"),
        QStringLiteral("Trace"), QStringLiteral("bool"),
        QStringLiteral("One row per CAN ID; new frames refresh that row"));
    add(QStringLiteral("trace.autoScroll"), QStringLiteral("Auto-scroll"),
        QStringLiteral("Trace"), QStringLiteral("bool"),
        QStringLiteral("Scroll to newest frames on arrival"));
    add(QStringLiteral("trace.showGrid"), QStringLiteral("Show grid"),
        QStringLiteral("Trace"), QStringLiteral("bool"),
        QStringLiteral("Draw grid lines in the Trace table"));
    add(QStringLiteral("trace.alternatingRowColors"), QStringLiteral("Alternating row colors"),
        QStringLiteral("Trace"), QStringLiteral("bool"),
        QStringLiteral("Alternate row background colors"));
    add(QStringLiteral("capture.maxFrames"), QStringLiteral("Capture ring size"),
        QStringLiteral("Trace"), QStringLiteral("int"),
        QStringLiteral("Process-wide CaptureLog capacity (Trace display ring is trace.maxFrames)"));

    add(QStringLiteral("graphic.timeWindow"), QStringLiteral("Time window (s)"),
        QStringLiteral("Graphic"), QStringLiteral("double"),
        QStringLiteral("Show the most recent N seconds of waveform data"));
    add(QStringLiteral("graphic.antialiasing"), QStringLiteral("Antialiasing"),
        QStringLiteral("Graphic"), QStringLiteral("bool"),
        QStringLiteral("Enable antialiased waveform rendering"));
    add(QStringLiteral("graphic.fps"), QStringLiteral("Refresh rate (FPS)"),
        QStringLiteral("Graphic"), QStringLiteral("int"),
        QStringLiteral("Waveform refresh frame rate"));
    add(QStringLiteral("graphic.maxSamples"), QStringLiteral("Max samples per signal"),
        QStringLiteral("Graphic"), QStringLiteral("int"),
        QStringLiteral("Ring buffer cap; oldest points overwritten when full"));
    add(QStringLiteral("graphic.overlayAutoThreshold"), QStringLiteral("Auto overlay threshold"),
        QStringLiteral("Graphic"), QStringLiteral("int"),
        QStringLiteral("Switch to overlay Y-axis when signal count reaches this (default 2; 0=disable)"));

    add(QStringLiteral("record.defaultFormat"), QStringLiteral("Default record format"),
        QStringLiteral("Record"), QStringLiteral("combo"),
        QStringLiteral("Default format for new recordings"),
        {QStringLiteral("openbus"), QStringLiteral("asc"), QStringLiteral("blf")});
    add(QStringLiteral("record.autoSave"), QStringLiteral("Auto-save"),
        QStringLiteral("Record"), QStringLiteral("bool"),
        QStringLiteral("Save automatically when recording stops"));

    add(QStringLiteral("log.level"), QStringLiteral("Log level"), QStringLiteral("Log"),
        QStringLiteral("combo"), QStringLiteral("spdlog output level"),
        {QStringLiteral("trace"), QStringLiteral("debug"), QStringLiteral("info"),
         QStringLiteral("warn"), QStringLiteral("error"), QStringLiteral("critical")});
    add(QStringLiteral("log.maxFileSize"), QStringLiteral("Max log file bytes"),
        QStringLiteral("Log"), QStringLiteral("int"),
        QStringLiteral("Maximum bytes per log file"));
    add(QStringLiteral("log.maxFiles"), QStringLiteral("Max log files"),
        QStringLiteral("Log"), QStringLiteral("int"),
        QStringLiteral("Rotated log file retention count"));
}

void SettingsPage::fillLanguageCombo()
{
    if (!m_languageCombo)
        return;
    m_updatingLanguageCombo = true;
    m_languageCombo->clear();
    const QString cur = TranslationManager::instance()->language();
    int sel = 0;
    const auto infos = TranslationManager::instance()->availableLanguageInfos();
    for (int i = 0; i < infos.size(); ++i) {
        m_languageCombo->addItem(infos[i].nativeName, infos[i].code);
        if (infos[i].code == cur)
            sel = i;
    }
    m_languageCombo->setCurrentIndex(sel);
    m_updatingLanguageCombo = false;
}

void SettingsPage::retranslateUi()
{
    setWindowTitle(tr("Settings"));
    if (m_searchEdit)
        m_searchEdit->setPlaceholderText(tr("Search settings..."));
    if (m_jsonBtn) {
        m_jsonBtn->setText(m_jsonBtn->isChecked() ? tr("Settings list") : tr("Edit JSON"));
    }
    if (m_settingsTree)
        m_settingsTree->setHeaderLabels({tr("Setting"), tr("Value")});
    if (m_resetBtn)
        m_resetBtn->setText(tr("Reset to defaults"));
    if (m_saveBtn)
        m_saveBtn->setText(tr("Save"));

    setupMetas();
    const QString cat = m_categoryTree && m_categoryTree->currentItem()
        ? m_categoryTree->currentItem()->data(0, Qt::UserRole).toString()
        : QString();
    populateCategoryTree();
    if (!cat.isEmpty())
        setCategory(cat);
    else
        populateSettingsTree(QString(), m_searchEdit ? m_searchEdit->text() : QString());
}

void SettingsPage::populateCategoryTree()
{
    QStringList categories;
    for (const auto &m : m_metas) {
        if (!categories.contains(m.category))
            categories << m.category;
    }

    m_categoryTree->clear();
    auto *allItem = new QTreeWidgetItem({tr("All Settings")});
    allItem->setData(0, Qt::UserRole, QString());
    m_categoryTree->addTopLevelItem(allItem);

    for (const auto &cat : categories) {
        auto *item = new QTreeWidgetItem({cat});
        item->setData(0, Qt::UserRole, cat);
        m_categoryTree->addTopLevelItem(item);
    }

    m_categoryTree->setCurrentItem(allItem);
}

void SettingsPage::populateSettingsTree(const QString &categoryFilter, const QString &textFilter)
{
    m_settingsTree->clear();
    m_languageCombo = nullptr;

    auto *cfg = AppConfig::instance();

    for (const auto &m : m_metas) {
        if (!categoryFilter.isEmpty() && m.category != categoryFilter)
            continue;
        if (!textFilter.isEmpty()) {
            if (!m.key.contains(textFilter, Qt::CaseInsensitive) &&
                !m.label.contains(textFilter, Qt::CaseInsensitive) &&
                !m.desc.contains(textFilter, Qt::CaseInsensitive))
                continue;
        }

        auto *item = new QTreeWidgetItem();
        item->setText(0, m.label);
        item->setToolTip(0, m.key + QLatin1Char('\n') + m.desc);
        item->setData(0, Qt::UserRole, m.key);

        QWidget *editor = nullptr;

        if (m.type == QLatin1String("language")) {
            auto *combo = new QComboBox();
            m_languageCombo = combo;
            fillLanguageCombo();
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &SettingsPage::onLanguageComboChanged);
            editor = combo;
            item->setText(1, combo->currentText());
        } else if (m.type == QLatin1String("bool")) {
            auto *chk = new QCheckBox();
            chk->setChecked(cfg->getBool(m.key, false));
            connect(chk, &QCheckBox::toggled, this, [cfg, key = m.key](bool v) {
                cfg->set(key, v);
            });
            editor = chk;
            item->setText(1, cfg->getBool(m.key, false) ? QStringLiteral("true")
                                                        : QStringLiteral("false"));
        } else if (m.type == QLatin1String("int")) {
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
        } else if (m.type == QLatin1String("double")) {
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
        } else if (m.type == QLatin1String("combo")) {
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
        } else {
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
            editor->setToolTip(m.key + QLatin1Char('\n') + m.desc);
            m_settingsTree->setItemWidget(item, 1, editor);
        }

        m_settingsTree->addTopLevelItem(item);
    }

    m_statusLabel->setText(tr("%1 settings").arg(m_settingsTree->topLevelItemCount()));
}

void SettingsPage::onLanguageComboChanged(int index)
{
    if (m_updatingLanguageCombo || !m_languageCombo || index < 0)
        return;
    const QString code = m_languageCombo->itemData(index).toString();
    if (code.isEmpty())
        return;
    TranslationManager::instance()->setLanguage(code);
}

void SettingsPage::setCategory(const QString &category)
{
    for (int i = 0; i < m_categoryTree->topLevelItemCount(); ++i) {
        auto *item = m_categoryTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == category) {
            m_categoryTree->setCurrentItem(item);
            populateSettingsTree(category, m_searchEdit->text());
            return;
        }
    }
    if (auto *all = m_categoryTree->topLevelItem(0)) {
        m_categoryTree->setCurrentItem(all);
        populateSettingsTree(QString(), m_searchEdit->text());
    }
}

void SettingsPage::onSearchChanged(const QString &text)
{
    auto *catItem = m_categoryTree->currentItem();
    QString cat = catItem ? catItem->data(0, Qt::UserRole).toString() : QString();
    populateSettingsTree(cat, text);
}

void SettingsPage::onCategorySelected(QTreeWidgetItem *item)
{
    if (!item) return;
    QString cat = item->data(0, Qt::UserRole).toString();
    populateSettingsTree(cat, m_searchEdit->text());
}

void SettingsPage::onJsonEdited()
{
    m_statusLabel->setText(tr("JSON modified — click Save to apply"));
}

void SettingsPage::onSave()
{
    if (m_rightStack->currentIndex() == 1) {
        QString jsonStr = m_jsonEdit->toPlainText();
        if (!AppConfig::instance()->fromJsonString(jsonStr)) {
            m_statusLabel->setText(tr("JSON parse failed — check syntax"));
            return;
        }
    }
    AppConfig::instance()->save();
    m_statusLabel->setText(tr("Saved"));
    spdlog::info("SettingsPage: config saved");
}

void SettingsPage::onReset()
{
    AppConfig::instance()->fromJsonString(
        QString::fromStdString(AppConfig::defaultConfig().dump(4)));
    populateSettingsTree(QString(), m_searchEdit->text());
    m_jsonEdit->setPlainText(AppConfig::instance()->toJsonString());
    m_statusLabel->setText(tr("Reset to defaults"));
}

void SettingsPage::switchToJsonPage()
{
    m_rightStack->setCurrentIndex(1);
}

void SettingsPage::switchToSettingsPage()
{
    m_rightStack->setCurrentIndex(0);
}
