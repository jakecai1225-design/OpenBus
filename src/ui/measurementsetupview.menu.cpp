#include "measurementsetupview.h"
#include "ui/measurementsetupview.gfx.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"
#include <QMenu>
#include <QAction>
#include <QPushButton>

// ============================================================
//  Right-click menu implementation — 右键菜单实现
// ============================================================

void MeasurementSetupView::onSceneRightClicked(const QPointF &scenePos)
{
    // Check if clicked within an instance sub-rect
    QString instModuleId, instInstanceId;
    if (instanceAt(scenePos, instModuleId, instInstanceId)) {
        if (!m_rightMenu)
            m_rightMenu = new QMenu(this);
        else
            m_rightMenu->clear();
        
        auto *titleAct = m_rightMenu->addAction(QString::fromUtf8("【 %1 】").arg(instInstanceId));
        titleAct->setEnabled(false);
        QFont titleFont = titleAct->font();
        titleFont.setBold(true);
        titleAct->setFont(titleFont);
        m_rightMenu->addSeparator();
        
        auto *actJump = m_rightMenu->addAction("跳转到此标签页");
        connect(actJump, &QAction::triggered, this, [this, instModuleId, instInstanceId]() {
            emit moduleOpened(instModuleId, instInstanceId);
        });
        
        auto *actClose = m_rightMenu->addAction("删除此实例");
        connect(actClose, &QAction::triggered, this, [this, instModuleId, instInstanceId]() {
            emit moduleInstanceClosed(instModuleId, instInstanceId);
        });
    } else {
        auto *b = blockAt(scenePos);
        if (b) {
            buildContextMenu(b, scenePos);
        } else {
            buildEmptyAreaMenu(scenePos);
        }
    }
    
    // Convert scene → view → global screen coordinates
    QPoint globalPos = m_view->mapToGlobal(m_view->mapFromScene(scenePos));
    m_rightMenu->exec(globalPos);
}

void MeasurementSetupView::buildContextMenu(BlockItem *block, const QPointF &)
{
    if (!block) return;
    
    if (!m_rightMenu)
        m_rightMenu = new QMenu(this);
    else
        m_rightMenu->clear();
    
    // Capture key properties by value to avoid dangling pointers in lambdas
    const QString blockId = block->id;
    const QString blockTitle = block->title;
    const QString blockCategory = block->category;
    const QString blockModule = block->moduleName;
    const bool blockEnabled = block->enabled;
    
    // -- Title action (disabled) --
    auto *titleAct = m_rightMenu->addAction(QString("【 %1 】").arg(blockTitle));
    titleAct->setEnabled(false);
    QFont titleFont = titleAct->font();
    titleFont.setBold(true);
    titleAct->setFont(titleFont);
    m_rightMenu->addSeparator();
    
    // ---- Data source / injector blocks ----
    if (blockCategory == "source") {
        if (blockId == "source_real" || blockId == "source_file") {
            if (blockId == "source_real") {
                auto *actSwitch = m_rightMenu->addAction(QStringLiteral("Switch to Offline Analysis"));
                actSwitch->setStatusTip(QStringLiteral("Switch to offline analysis mode"));
                connect(actSwitch, &QAction::triggered, this, [this]() {
                    setSource(Source::File);
                    emit sourceChanged(static_cast<int>(Source::File));
                });
            } else {
                auto *actSwitch = m_rightMenu->addAction(QStringLiteral("Switch to Real"));
                actSwitch->setStatusTip(QStringLiteral("Switch to hardware real-time acquisition"));
                connect(actSwitch, &QAction::triggered, this, [this]() {
                    setSource(Source::Hardware);
                    emit sourceChanged(static_cast<int>(Source::Hardware));
                });
            }
            m_rightMenu->addSeparator();
            auto *actCfg = m_rightMenu->addAction(
                blockId == "source_real" ? QStringLiteral("Device settings...")
                                         : QStringLiteral("Open Offline Analysis..."));
            connect(actCfg, &QAction::triggered, this, [this, blockId]() {
                if (blockId == "source_real")
                    emit realBlockClicked();
                else
                    emit fileBlockClicked();
            });
        } else if (blockId == "signal_generator") {
            auto *actOpen = m_rightMenu->addAction(QStringLiteral("Open Signal Send..."));
            connect(actOpen, &QAction::triggered, this, [this]() {
                emit sendPageOpened();
            });
        } else if (blockId == "file_playback") {
            auto *actOpen = m_rightMenu->addAction(QStringLiteral("Open File Playback..."));
            connect(actOpen, &QAction::triggered, this, [this]() {
                emit playbackPageOpened();
            });
        }
    }
    
    // ---- Filter block ----
    else if (blockCategory == "filter") {
        auto *actFilter = m_rightMenu->addAction(QStringLiteral("Configure filter rules..."));
        actFilter->setStatusTip(QStringLiteral("Set CAN ID ranges, frame types, direction, etc."));
        connect(actFilter, &QAction::triggered, this, [this, blockId]() {
            setBlockEnabled(blockId, true);
            openBlockConfig(blockId);
        });

        const int ruleCount = block->instances.size();
        if (ruleCount > 0) {
            auto *actClear = m_rightMenu->addAction(
                QStringLiteral("Clear filter rules (%1)").arg(ruleCount));
            actClear->setStatusTip(QStringLiteral("Remove all rules; stream passes unfiltered"));
            connect(actClear, &QAction::triggered, this, [this]() {
                clearFilterRules();
            });
        }

        m_rightMenu->addSeparator();

        auto *actToggle = m_rightMenu->addAction(
            blockEnabled ? QStringLiteral("Disable filter") : QStringLiteral("Enable filter"));
        connect(actToggle, &QAction::triggered, this, [this, blockId]() {
            toggleBlock(blockId);
        });
    }

    // ---- Database / CAN parser block ----
    else if (blockCategory == "database") {
        auto *actDbc = m_rightMenu->addAction(QStringLiteral("Select DBC file..."));
        actDbc->setStatusTip(QStringLiteral("Select from currently loaded DBC files"));
        connect(actDbc, &QAction::triggered, this, [this, blockId]() {
            setBlockEnabled(blockId, true);
            openBlockConfig(blockId);
        });

        m_rightMenu->addSeparator();

        if (!m_dbcFiles.isEmpty()) {
            auto *dbcListAct = m_rightMenu->addAction(
                QStringLiteral("Loaded DBC: %1").arg(m_dbcFiles.size()));
            dbcListAct->setEnabled(false);
            m_rightMenu->addSeparator();
            for (const auto &name : m_dbcFiles) {
                auto *act = m_rightMenu->addAction(QStringLiteral("Remove  %1").arg(name));
                act->setStatusTip(QStringLiteral("Unload this DBC file from the project"));
                connect(act, &QAction::triggered, this, [this, name]() {
                    emit dbcRemoveRequested(name);
                });
            }
        } else {
            auto *noDbc = m_rightMenu->addAction(QStringLiteral("(no DBC loaded)"));
            noDbc->setEnabled(false);
        }
    }
    
    // ---- Module blocks ----
    else if (blockCategory == "module") {
        // Add instance action based on module type
        if (blockModule == "trace") {
            // Trace independent block: jump + delete (no add, add at empty area menu)
            auto *actAdd = m_rightMenu->addAction(
                svgIcon(":/icons/plus.svg", ThemeManager::instance()->currentTheme().text, 16),
                "添加 Trace 视图");
            actAdd->setStatusTip("Create a new Trace message list block");
            connect(actAdd, &QAction::triggered, this, [this]() {
                emit moduleOpened("trace", "");
            });
        } else if (blockModule == "graphic") {
            auto *actAdd = m_rightMenu->addAction("添加 Graphic 波形");
            actAdd->setStatusTip("Create a new Graphic waveform plot tab");
            connect(actAdd, &QAction::triggered, this, [this]() {
                emit moduleOpened("graphic", "");
            });
        } else if (blockId == "watcher") {
            auto *actCfg = m_rightMenu->addAction("观测变量与统计设置...");
            actCfg->setStatusTip("Open Watcher observation page (variables Watch list + bus statistics)");
            connect(actCfg, &QAction::triggered, this, [this]() {
                emit moduleOpened("watcher", "");
            });
        } else if (blockId == "record") {
            auto *actCfg = m_rightMenu->addAction(" 配置录制参数...");
            actCfg->setStatusTip("Configure recording file path and format");
            connect(actCfg, &QAction::triggered, this, [this]() {
                emit moduleOpened("record", "");
            });
        }
        
        m_rightMenu->addSeparator();
        
        // Unified configuration entry: open/switch corresponding tab
        auto *actOpen = m_rightMenu->addAction("配置 / 跳转标签页");
        actOpen->setStatusTip("Open/switch to this module's tab in the center area");
        connect(actOpen, &QAction::triggered, this, [this, blockModule, blockId]() {
            if (blockModule == "trace" || blockModule == "graphic")
                emit moduleOpened(blockModule, blockId);
            else
                emit moduleOpened(blockId, "");
        });
        
        m_rightMenu->addSeparator();
        
        auto *actToggle = m_rightMenu->addAction(blockEnabled ? "禁用模块" : "启用模块");
        connect(actToggle, &QAction::triggered, this, [this, blockId]() {
            toggleBlock(blockId);
        });
        
        m_rightMenu->addSeparator();
        
        // Trace/Graphic: delete instance; other modules: delete block
        auto *actDelMod = m_rightMenu->addAction(
            blockModule == "trace"   ? "删除此 Trace" :
            blockModule == "graphic" ? "删除此 Graphic" :
                                      "删除此模块块");
        connect(actDelMod, &QAction::triggered, this, [this, blockModule, blockId]() {
            if (blockModule == "trace" || blockModule == "graphic")
                emit moduleInstanceClosed(blockModule, blockId);
            else
                removeModuleBlock(blockId);
        });
    }
}

void MeasurementSetupView::buildEmptyAreaMenu(const QPointF &)
{
    if (!m_rightMenu)
        m_rightMenu = new QMenu(this);
    else
        m_rightMenu->clear();
    
    auto *titleAct = m_rightMenu->addAction(QString::fromUtf8("【 添加配置块 】"));
    titleAct->setEnabled(false);
    QFont titleFont = titleAct->font();
    titleFont.setBold(true);
    titleAct->setFont(titleFont);
    m_rightMenu->addSeparator();
    
    // Trace: always can add new block
    auto *actAddTrace = m_rightMenu->addAction("添加 Trace 视图");
    connect(actAddTrace, &QAction::triggered, this, [this]() {
        emit moduleOpened("trace", "");
    });
    
    // Graphic: always can add new block
    auto *actAddGraphic = m_rightMenu->addAction("添加 Graphic 波形");
    connect(actAddGraphic, &QAction::triggered, this, [this]() {
        emit moduleOpened("graphic", "");
    });
    
    // Show add options only when that module type doesn't already exist
    struct ModDef { QString id; QString icon; QString title; QColor color; };
    ModDef stdMods[] = {
        {"watcher",  "", "Watcher",          QColor(0x4C, 0xAF, 0x50)},
        {"record",   "", "Record",           QColor(0xFF, 0x98, 0x00)},
    };
    
    for (const auto &mod : stdMods) {
        if (!m_blocks.contains(mod.id)) {
            auto *actAdd = m_rightMenu->addAction(QString("添加 %1").arg(mod.title));
            connect(actAdd, &QAction::triggered, this, [this, mod]() {
                const qreal modW = 140;
                const qreal modH = 60;
                const qreal modGap = 16;
                
                // Stack vertically below module area
                qreal maxY = 0;
                qreal modX = 0;
                for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
                    if (it.value().category == "module") {
                        maxY = qMax(maxY, it.value().rect.bottom());
                        modX = it.value().rect.left();
                    }
                }
                if (maxY == 0) {
                    auto dbIt = m_blocks.find("database");
                    modX = (dbIt != m_blocks.end()) ? dbIt->rect.right() + 60 : 750;
                    maxY = 30;
                }
                
                BlockItem b;
                b.id = mod.id;
                b.title = mod.title;
                b.icon = mod.icon;
                b.category = "module";
                b.color = mod.color;
                b.rect = QRectF(modX, maxY + modGap, modW, modH);
                m_blocks[mod.id] = b;
                
                Connection c;
                c.fromId = "database";
                c.toId = mod.id;
                c.pathItem = nullptr;
                m_connections.append(c);
                
                rebuildScene();
            });
        }
    }
}
