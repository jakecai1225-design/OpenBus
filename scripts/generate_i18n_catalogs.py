#!/usr/bin/env python3
"""Generate openbus .ts catalogs and Python locale JSON stubs.

Source language is English. Re-run after adding tr()/t() strings, then rebuild
so CMake/lrelease produces .qm next to the exe.

Usage:
  python scripts/generate_i18n_catalogs.py
"""

from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.dom import minidom

ROOT = Path(__file__).resolve().parents[1]
TS_DIR = ROOT / "translations"
PY_DIR = ROOT / "plugins" / "_shared" / "locales"

LOCALES = ["zh_CN", "zh_TW", "es", "fr", "de", "ja", "pt_BR", "ru", "ko"]

# context -> list of (source, {locale: translation})
# Empty translation falls back to English at runtime.
STRINGS: dict[str, list[tuple[str, dict[str, str]]]] = {
    "MainWindow": [
        ("File(&F)", {
            "zh_CN": "文件(&F)", "zh_TW": "檔案(&F)", "es": "Archivo(&F)",
            "fr": "Fichier(&F)", "de": "Datei(&F)", "ja": "ファイル(&F)",
            "pt_BR": "Arquivo(&F)", "ru": "Файл(&F)", "ko": "파일(&F)",
        }),
        ("Open File...", {
            "zh_CN": "打开文件...", "zh_TW": "開啟檔案...", "es": "Abrir archivo...",
            "fr": "Ouvrir un fichier...", "de": "Datei öffnen...", "ja": "ファイルを開く...",
            "pt_BR": "Abrir arquivo...", "ru": "Открыть файл...", "ko": "파일 열기...",
        }),
        ("Open message file (BLF/ASC/CSV/PCAP/TRC) or DBC file", {
            "zh_CN": "打开报文文件 (BLF/ASC/CSV/PCAP/TRC) 或 DBC 文件",
            "zh_TW": "開啟報文檔案 (BLF/ASC/CSV/PCAP/TRC) 或 DBC 檔案",
            "es": "Abrir archivo de mensajes o DBC",
            "fr": "Ouvrir un fichier de messages ou DBC",
            "de": "Nachrichtendatei oder DBC öffnen",
            "ja": "メッセージファイルまたは DBC を開く",
            "pt_BR": "Abrir arquivo de mensagens ou DBC",
            "ru": "Открыть файл сообщений или DBC",
            "ko": "메시지 파일 또는 DBC 열기",
        }),
        ("Open Project...", {
            "zh_CN": "打开工程...", "zh_TW": "開啟專案...", "es": "Abrir proyecto...",
            "fr": "Ouvrir le projet...", "de": "Projekt öffnen...", "ja": "プロジェクトを開く...",
            "pt_BR": "Abrir projeto...", "ru": "Открыть проект...", "ko": "프로젝트 열기...",
        }),
        ("Save Project", {
            "zh_CN": "保存工程", "zh_TW": "儲存專案", "es": "Guardar proyecto",
            "fr": "Enregistrer le projet", "de": "Projekt speichern", "ja": "プロジェクトを保存",
            "pt_BR": "Salvar projeto", "ru": "Сохранить проект", "ko": "프로젝트 저장",
        }),
        ("Import Log File...", {
            "zh_CN": "导入日志文件...", "zh_TW": "匯入日誌檔案...", "es": "Importar registro...",
            "fr": "Importer un journal...", "de": "Logdatei importieren...", "ja": "ログをインポート...",
            "pt_BR": "Importar log...", "ru": "Импорт журнала...", "ko": "로그 가져오기...",
        }),
        ("Import BLF/ASC/CSV log into Trace", {
            "zh_CN": "导入 BLF/ASC/CSV 日志文件到 Trace",
            "zh_TW": "匯入 BLF/ASC/CSV 日誌到 Trace",
            "es": "Importar log BLF/ASC/CSV a Trace",
            "fr": "Importer un journal BLF/ASC/CSV dans Trace",
            "de": "BLF/ASC/CSV-Log in Trace importieren",
            "ja": "BLF/ASC/CSV ログを Trace にインポート",
            "pt_BR": "Importar log BLF/ASC/CSV para Trace",
            "ru": "Импорт BLF/ASC/CSV в Trace",
            "ko": "BLF/ASC/CSV 로그를 Trace로 가져오기",
        }),
        ("E&xit", {
            "zh_CN": "退出(&Q)", "zh_TW": "結束(&Q)", "es": "Salir(&Q)",
            "fr": "Quitter(&Q)", "de": "Beenden(&Q)", "ja": "終了(&Q)",
            "pt_BR": "Sair(&Q)", "ru": "Выход(&Q)", "ko": "종료(&Q)",
        }),
        ("View(&V)", {
            "zh_CN": "视图(&V)", "zh_TW": "檢視(&V)", "es": "Ver(&V)",
            "fr": "Affichage(&V)", "de": "Ansicht(&V)", "ja": "表示(&V)",
            "pt_BR": "Exibir(&V)", "ru": "Вид(&V)", "ko": "보기(&V)",
        }),
        ("Left Sidebar", {
            "zh_CN": "左侧栏", "zh_TW": "左側欄", "es": "Barra izquierda",
            "fr": "Barre latérale gauche", "de": "Linke Seitenleiste", "ja": "左サイドバー",
            "pt_BR": "Barra esquerda", "ru": "Левая панель", "ko": "왼쪽 사이드바",
        }),
        ("Bottom Panel", {
            "zh_CN": "底部栏", "zh_TW": "底部面板", "es": "Panel inferior",
            "fr": "Panneau inférieur", "de": "Untere Leiste", "ja": "下部パネル",
            "pt_BR": "Painel inferior", "ru": "Нижняя панель", "ko": "하단 패널",
        }),
        ("Right Sidebar", {
            "zh_CN": "右侧栏", "zh_TW": "右側欄", "es": "Barra derecha",
            "fr": "Barre latérale droite", "de": "Rechte Seitenleiste", "ja": "右サイドバー",
            "pt_BR": "Barra direita", "ru": "Правая панель", "ko": "오른쪽 사이드바",
        }),
        ("Welcome", {
            "zh_CN": "欢迎", "zh_TW": "歡迎", "es": "Bienvenida",
            "fr": "Bienvenue", "de": "Willkommen", "ja": "ようこそ",
            "pt_BR": "Boas-vindas", "ru": "Добро пожаловать", "ko": "시작",
        }),
        ("Reset Layout", {
            "zh_CN": "重置布局", "zh_TW": "重設版面", "es": "Restablecer diseño",
            "fr": "Réinitialiser la disposition", "de": "Layout zurücksetzen", "ja": "レイアウトをリセット",
            "pt_BR": "Redefinir layout", "ru": "Сбросить макет", "ko": "레이아웃 초기화",
        }),
        ("Tools(&T)", {
            "zh_CN": "工具(&T)", "zh_TW": "工具(&T)", "es": "Herramientas(&T)",
            "fr": "Outils(&T)", "de": "Extras(&T)", "ja": "ツール(&T)",
            "pt_BR": "Ferramentas(&T)", "ru": "Сервис(&T)", "ko": "도구(&T)",
        }),
        ("Data Window", {}),
        ("I/O Graph", {}),
        ("Watcher", {
            "zh_CN": "Watcher 观测", "zh_TW": "Watcher 觀測", "es": "Watcher",
            "fr": "Watcher", "de": "Watcher", "ja": "Watcher",
            "pt_BR": "Watcher", "ru": "Watcher", "ko": "Watcher",
        }),
        ("Color Rule Editor...", {
            "zh_CN": "着色规则编辑器...", "zh_TW": "著色規則編輯器...",
            "es": "Editor de reglas de color...", "fr": "Éditeur de règles de couleur...",
            "de": "Farbregel-Editor...", "ja": "色ルールエディタ...",
            "pt_BR": "Editor de regras de cor...", "ru": "Редактор правил цвета...",
            "ko": "색상 규칙 편집기...",
        }),
        ("Record", {
            "zh_CN": "录制", "zh_TW": "錄製", "es": "Grabar",
            "fr": "Enregistrer", "de": "Aufnehmen", "ja": "記録",
            "pt_BR": "Gravar", "ru": "Запись", "ko": "녹화",
        }),
        ("Play", {
            "zh_CN": "播放", "zh_TW": "播放", "es": "Reproducir",
            "fr": "Lecture", "de": "Wiedergeben", "ja": "再生",
            "pt_BR": "Reproduzir", "ru": "Воспроизведение", "ko": "재생",
        }),
        ("Pause", {
            "zh_CN": "暂停", "zh_TW": "暫停", "es": "Pausa",
            "fr": "Pause", "de": "Pause", "ja": "一時停止",
            "pt_BR": "Pausar", "ru": "Пауза", "ko": "일시정지",
        }),
        ("Stop", {
            "zh_CN": "停止", "zh_TW": "停止", "es": "Detener",
            "fr": "Arrêt", "de": "Stopp", "ja": "停止",
            "pt_BR": "Parar", "ru": "Стоп", "ko": "정지",
        }),
        ("Clear Trace", {
            "zh_CN": "清空 Trace", "zh_TW": "清除 Trace", "es": "Vaciar Trace",
            "fr": "Effacer Trace", "de": "Trace leeren", "ja": "Trace をクリア",
            "pt_BR": "Limpar Trace", "ru": "Очистить Trace", "ko": "Trace 지우기",
        }),
        ("Auto-scroll", {
            "zh_CN": "自动滚动", "zh_TW": "自動捲動", "es": "Desplazamiento automático",
            "fr": "Défilement auto", "de": "Auto-Scroll", "ja": "自動スクロール",
            "pt_BR": "Rolagem automática", "ru": "Автопрокрутка", "ko": "자동 스크롤",
        }),
        ("Simulator", {
            "zh_CN": "模拟器开关", "zh_TW": "模擬器開關", "es": "Simulador",
            "fr": "Simulateur", "de": "Simulator", "ja": "シミュレータ",
            "pt_BR": "Simulador", "ru": "Симулятор", "ko": "시뮬레이터",
        }),
        ("Help(&H)", {
            "zh_CN": "帮助(&H)", "zh_TW": "說明(&H)", "es": "Ayuda(&H)",
            "fr": "Aide(&H)", "de": "Hilfe(&H)", "ja": "ヘルプ(&H)",
            "pt_BR": "Ajuda(&H)", "ru": "Справка(&H)", "ko": "도움말(&H)",
        }),
        ("About openbus", {
            "zh_CN": "关于 openbus", "zh_TW": "關於 openbus", "es": "Acerca de openbus",
            "fr": "À propos d'openbus", "de": "Über openbus", "ja": "openbus について",
            "pt_BR": "Sobre o openbus", "ru": "О программе openbus", "ko": "openbus 정보",
        }),
        ("Documentation", {
            "zh_CN": "文档", "zh_TW": "文件", "es": "Documentación",
            "fr": "Documentation", "de": "Dokumentation", "ja": "ドキュメント",
            "pt_BR": "Documentação", "ru": "Документация", "ko": "문서",
        }),
        ("Ready", {
            "zh_CN": "就绪", "zh_TW": "就緒", "es": "Listo",
            "fr": "Prêt", "de": "Bereit", "ja": "準備完了",
            "pt_BR": "Pronto", "ru": "Готово", "ko": "준비됨",
        }),
    ],
    "ActivityBar": [
        ("Project", {
            "zh_CN": "工程管理", "zh_TW": "專案管理", "es": "Proyecto",
            "fr": "Projet", "de": "Projekt", "ja": "プロジェクト",
            "pt_BR": "Projeto", "ru": "Проект", "ko": "프로젝트",
        }),
        ("Flow", {}),
        ("flow — CANoe Measurement Setup style", {
            "zh_CN": "flow — CANoe Measurement Setup 风格",
            "zh_TW": "flow — CANoe Measurement Setup 風格",
            "es": "flow — estilo Measurement Setup de CANoe",
            "fr": "flow — style Measurement Setup CANoe",
            "de": "flow — CANoe Measurement Setup-Stil",
            "ja": "flow — CANoe Measurement Setup 風",
            "pt_BR": "flow — estilo Measurement Setup do CANoe",
            "ru": "flow — стиль Measurement Setup CANoe",
            "ko": "flow — CANoe Measurement Setup 스타일",
        }),
        ("Devices", {
            "zh_CN": "设备连接", "zh_TW": "裝置連線", "es": "Dispositivos",
            "fr": "Périphériques", "de": "Geräte", "ja": "デバイス",
            "pt_BR": "Dispositivos", "ru": "Устройства", "ko": "장치",
        }),
        ("Trace", {}),
        ("Graphic", {}),
        ("Database", {
            "zh_CN": "数据库", "zh_TW": "資料庫", "es": "Base de datos",
            "fr": "Base de données", "de": "Datenbank", "ja": "データベース",
            "pt_BR": "Banco de dados", "ru": "База данных", "ko": "데이터베이스",
        }),
        ("Database — multi-protocol parse file management", {
            "zh_CN": "数据库 — 多协议解析文件管理",
            "zh_TW": "資料庫 — 多協定解析檔案管理",
            "es": "Base de datos — gestión de archivos de parseo",
            "fr": "Base de données — gestion des fichiers de parsing",
            "de": "Datenbank — Verwaltung von Parse-Dateien",
            "ja": "データベース — マルチプロトコル解析ファイル管理",
            "pt_BR": "Banco de dados — gerenciamento de arquivos de parse",
            "ru": "База данных — управление файлами разбора",
            "ko": "데이터베이스 — 다중 프로토콜 파싱 파일 관리",
        }),
        ("Transceive", {
            "zh_CN": "收发", "zh_TW": "收發", "es": "Tx/Rx",
            "fr": "Émission/Réception", "de": "Senden/Empfangen", "ja": "送受信",
            "pt_BR": "Tx/Rx", "ru": "Приём/передача", "ko": "송수신",
        }),
        ("Transceive — send / playback / record", {
            "zh_CN": "收发 — 发送 / 回放 / 录制",
            "zh_TW": "收發 — 傳送 / 回放 / 錄製",
            "es": "Tx/Rx — enviar / reproducción / grabar",
            "fr": "Émission/Réception — envoi / lecture / enregistrement",
            "de": "Senden/Empfangen — Senden / Wiedergabe / Aufnehmen",
            "ja": "送受信 — 送信 / 再生 / 記録",
            "pt_BR": "Tx/Rx — envio / reprodução / gravação",
            "ru": "Приём/передача — отправка / воспроизведение / запись",
            "ko": "송수신 — 송신 / 재생 / 녹화",
        }),
        ("Extensions", {
            "zh_CN": "插件市场", "zh_TW": "擴充功能市集", "es": "Extensiones",
            "fr": "Extensions", "de": "Erweiterungen", "ja": "拡張機能",
            "pt_BR": "Extensões", "ru": "Расширения", "ko": "확장",
        }),
        ("Extensions — install / manage / search drivers and plugins", {
            "zh_CN": "插件市场 — 驱动与插件的安装 / 管理 / 搜索",
            "zh_TW": "擴充功能市集 — 驅動與外掛的安裝 / 管理 / 搜尋",
            "es": "Extensiones — instalar / gestionar / buscar",
            "fr": "Extensions — installer / gérer / rechercher",
            "de": "Erweiterungen — installieren / verwalten / suchen",
            "ja": "拡張機能 — ドライバとプラグインのインストール / 管理 / 検索",
            "pt_BR": "Extensões — instalar / gerenciar / pesquisar",
            "ru": "Расширения — установка / управление / поиск",
            "ko": "확장 — 드라이버 및 플러그인 설치 / 관리 / 검색",
        }),
        ("Account", {
            "zh_CN": "账户", "zh_TW": "帳戶", "es": "Cuenta",
            "fr": "Compte", "de": "Konto", "ja": "アカウント",
            "pt_BR": "Conta", "ru": "Учётная запись", "ko": "계정",
        }),
        ("Settings", {
            "zh_CN": "设置", "zh_TW": "設定", "es": "Configuración",
            "fr": "Paramètres", "de": "Einstellungen", "ja": "設定",
            "pt_BR": "Configurações", "ru": "Параметры", "ko": "설정",
        }),
    ],
    "SettingsPage": [
        ("Settings", {
            "zh_CN": "设置", "zh_TW": "設定", "es": "Configuración",
            "fr": "Paramètres", "de": "Einstellungen", "ja": "設定",
            "pt_BR": "Configurações", "ru": "Параметры", "ko": "설정",
        }),
        ("Search settings...", {
            "zh_CN": "搜索设置...", "zh_TW": "搜尋設定...", "es": "Buscar configuración...",
            "fr": "Rechercher des paramètres...", "de": "Einstellungen suchen...", "ja": "設定を検索...",
            "pt_BR": "Pesquisar configurações...", "ru": "Поиск параметров...", "ko": "설정 검색...",
        }),
        ("Edit JSON", {
            "zh_CN": "编辑 JSON", "zh_TW": "編輯 JSON", "es": "Editar JSON",
            "fr": "Modifier JSON", "de": "JSON bearbeiten", "ja": "JSON を編集",
            "pt_BR": "Editar JSON", "ru": "Изменить JSON", "ko": "JSON 편집",
        }),
        ("Settings list", {
            "zh_CN": "设置列表", "zh_TW": "設定清單", "es": "Lista de ajustes",
            "fr": "Liste des paramètres", "de": "Einstellungsliste", "ja": "設定一覧",
            "pt_BR": "Lista de configurações", "ru": "Список параметров", "ko": "설정 목록",
        }),
        ("Setting", {
            "zh_CN": "设置项", "zh_TW": "設定項", "es": "Ajuste",
            "fr": "Paramètre", "de": "Einstellung", "ja": "設定項目",
            "pt_BR": "Configuração", "ru": "Параметр", "ko": "설정 항목",
        }),
        ("Value", {
            "zh_CN": "值", "zh_TW": "值", "es": "Valor",
            "fr": "Valeur", "de": "Wert", "ja": "値",
            "pt_BR": "Valor", "ru": "Значение", "ko": "값",
        }),
        ("Reset to defaults", {
            "zh_CN": "重置为默认", "zh_TW": "重設為預設", "es": "Restablecer valores",
            "fr": "Réinitialiser", "de": "Zurücksetzen", "ja": "既定にリセット",
            "pt_BR": "Restaurar padrões", "ru": "Сбросить", "ko": "기본값으로 재설정",
        }),
        ("Save", {
            "zh_CN": "保存", "zh_TW": "儲存", "es": "Guardar",
            "fr": "Enregistrer", "de": "Speichern", "ja": "保存",
            "pt_BR": "Salvar", "ru": "Сохранить", "ko": "저장",
        }),
        ("Language", {
            "zh_CN": "界面语言", "zh_TW": "介面語言", "es": "Idioma",
            "fr": "Langue", "de": "Sprache", "ja": "言語",
            "pt_BR": "Idioma", "ru": "Язык", "ko": "언어",
        }),
        ("UI language (applies immediately)", {
            "zh_CN": "界面语言（立即生效，无需重启）",
            "zh_TW": "介面語言（立即生效，無需重啟）",
            "es": "Idioma de la UI (se aplica al instante)",
            "fr": "Langue de l'interface (immédiat)",
            "de": "UI-Sprache (sofort wirksam)",
            "ja": "UI 言語（すぐに反映、再起動不要）",
            "pt_BR": "Idioma da UI (aplica imediatamente)",
            "ru": "Язык интерфейса (сразу)",
            "ko": "UI 언어 (즉시 적용)",
        }),
        ("General", {
            "zh_CN": "通用", "zh_TW": "一般", "es": "General",
            "fr": "Général", "de": "Allgemein", "ja": "一般",
            "pt_BR": "Geral", "ru": "Общие", "ko": "일반",
        }),
        ("All Settings", {
            "zh_CN": "全部设置", "zh_TW": "全部設定", "es": "Todos los ajustes",
            "fr": "Tous les paramètres", "de": "Alle Einstellungen", "ja": "すべての設定",
            "pt_BR": "Todas as configurações", "ru": "Все параметры", "ko": "모든 설정",
        }),
    ],
    "MeasurementSetupView": [
        ("Start", {
            "zh_CN": "开始", "zh_TW": "開始", "es": "Iniciar",
            "fr": "Démarrer", "de": "Start", "ja": "開始",
            "pt_BR": "Iniciar", "ru": "Старт", "ko": "시작",
        }),
        ("Replay", {
            "zh_CN": "重放", "zh_TW": "重播", "es": "Repetir",
            "fr": "Rejouer", "de": "Wiederholen", "ja": "リプレイ",
            "pt_BR": "Repetir", "ru": "Повтор", "ko": "리플레이",
        }),
        ("Stop", {
            "zh_CN": "停止", "zh_TW": "停止", "es": "Detener",
            "fr": "Arrêt", "de": "Stopp", "ja": "停止",
            "pt_BR": "Parar", "ru": "Стоп", "ko": "정지",
        }),
        ("Start / continue measurement", {
            "zh_CN": "开始 / 继续测量", "zh_TW": "開始 / 繼續量測",
            "es": "Iniciar / continuar medición", "fr": "Démarrer / continuer la mesure",
            "de": "Messung starten / fortsetzen", "ja": "計測を開始 / 続行",
            "pt_BR": "Iniciar / continuar medição", "ru": "Начать / продолжить измерение",
            "ko": "측정 시작 / 계속",
        }),
        ("Replay: clear Trace/Graphic and start from the beginning", {
            "zh_CN": "重放：清空 Trace/Graphic 并从开头开始",
            "zh_TW": "重播：清除 Trace/Graphic 並從頭開始",
            "es": "Repetir: vaciar Trace/Graphic y empezar de nuevo",
            "fr": "Rejouer : effacer Trace/Graphic et recommencer",
            "de": "Wiederholen: Trace/Graphic leeren und von vorn starten",
            "ja": "リプレイ: Trace/Graphic をクリアして最初から",
            "pt_BR": "Repetir: limpar Trace/Graphic e recomeçar",
            "ru": "Повтор: очистить Trace/Graphic и начать сначала",
            "ko": "리플레이: Trace/Graphic 지우고 처음부터",
        }),
        ("Stop measurement", {
            "zh_CN": "停止测量", "zh_TW": "停止量測", "es": "Detener medición",
            "fr": "Arrêter la mesure", "de": "Messung stoppen", "ja": "計測を停止",
            "pt_BR": "Parar medição", "ru": "Остановить измерение", "ko": "측정 정지",
        }),
        ("Real", {}),
        ("Offline Analysis", {
            "zh_CN": "离线分析", "zh_TW": "離線分析", "es": "Análisis offline",
            "fr": "Analyse hors ligne", "de": "Offline-Analyse", "ja": "オフライン解析",
            "pt_BR": "Análise offline", "ru": "Офлайн-анализ", "ko": "오프라인 분석",
        }),
        ("Signal Generator", {
            "zh_CN": "信号发生器", "zh_TW": "訊號產生器", "es": "Generador de señales",
            "fr": "Générateur de signaux", "de": "Signalgenerator", "ja": "信号発生器",
            "pt_BR": "Gerador de sinais", "ru": "Генератор сигналов", "ko": "신호 발생기",
        }),
        ("File Playback", {
            "zh_CN": "文件回放", "zh_TW": "檔案回放", "es": "Reproducción de archivo",
            "fr": "Lecture de fichier", "de": "Datei-Wiedergabe", "ja": "ファイル再生",
            "pt_BR": "Reprodução de arquivo", "ru": "Воспроизведение файла", "ko": "파일 재생",
        }),
        ("Filter", {
            "zh_CN": "过滤器", "zh_TW": "篩選器", "es": "Filtro",
            "fr": "Filtre", "de": "Filter", "ja": "フィルタ",
            "pt_BR": "Filtro", "ru": "Фильтр", "ko": "필터",
        }),
        ("CAN parser", {}),
    ],
    "FilterBar": [
        ("Display filter (e.g. id == 0x123 and fd)...", {
            "zh_CN": "显示过滤器（例如 id == 0x123 and fd）...",
            "zh_TW": "顯示篩選器（例如 id == 0x123 and fd）...",
            "es": "Filtro de visualización (p. ej. id == 0x123 and fd)...",
            "fr": "Filtre d'affichage (ex. id == 0x123 and fd)...",
            "de": "Anzeigefilter (z. B. id == 0x123 and fd)...",
            "ja": "表示フィルタ（例: id == 0x123 and fd）...",
            "pt_BR": "Filtro de exibição (ex.: id == 0x123 and fd)...",
            "ru": "Фильтр отображения (напр. id == 0x123 and fd)...",
            "ko": "표시 필터 (예: id == 0x123 and fd)...",
        }),
        ("Apply display filter (Enter)", {
            "zh_CN": "应用显示过滤器 (Enter)", "zh_TW": "套用顯示篩選器 (Enter)",
            "es": "Aplicar filtro (Enter)", "fr": "Appliquer le filtre (Entrée)",
            "de": "Anzeigefilter anwenden (Enter)", "ja": "表示フィルタを適用 (Enter)",
            "pt_BR": "Aplicar filtro (Enter)", "ru": "Применить фильтр (Enter)",
            "ko": "표시 필터 적용 (Enter)",
        }),
        ("Clear display filter expression", {
            "zh_CN": "清除显示过滤器表达式", "zh_TW": "清除顯示篩選運算式",
            "es": "Borrar expresión de filtro", "fr": "Effacer l'expression du filtre",
            "de": "Filterausdruck löschen", "ja": "フィルタ式をクリア",
            "pt_BR": "Limpar expressão do filtro", "ru": "Очистить выражение фильтра",
            "ko": "표시 필터 식 지우기",
        }),
        ("Filter syntax help", {
            "zh_CN": "过滤器语法帮助", "zh_TW": "篩選器語法說明",
            "es": "Ayuda de sintaxis", "fr": "Aide sur la syntaxe",
            "de": "Filter-Syntaxhilfe", "ja": "フィルタ構文ヘルプ",
            "pt_BR": "Ajuda de sintaxe", "ru": "Справка по синтаксису",
            "ko": "필터 구문 도움말",
        }),
        ("Filter presets", {
            "zh_CN": "过滤器预设", "zh_TW": "篩選器預設",
            "es": "Presets de filtro", "fr": "Préréglages de filtre",
            "de": "Filter-Presets", "ja": "フィルタプリセット",
            "pt_BR": "Predefinições de filtro", "ru": "Пресеты фильтра",
            "ko": "필터 프리셋",
        }),
        ("Trace settings (time format, overwrite, colors)", {
            "zh_CN": "Trace 设置（时间格式、覆盖、着色）",
            "zh_TW": "Trace 設定（時間格式、覆寫、著色）",
            "es": "Ajustes de Trace", "fr": "Paramètres Trace",
            "de": "Trace-Einstellungen", "ja": "Trace 設定",
            "pt_BR": "Configurações do Trace", "ru": "Настройки Trace",
            "ko": "Trace 설정",
        }),
        ("Clear list (delete all frames)", {
            "zh_CN": "清空列表（删除全部帧）", "zh_TW": "清除清單（刪除全部幀）",
            "es": "Vaciar lista (borrar todas las tramas)", "fr": "Vider la liste",
            "de": "Liste leeren", "ja": "リストをクリア",
            "pt_BR": "Limpar lista", "ru": "Очистить список",
            "ko": "목록 지우기",
        }),
        ("Clear all", {
            "zh_CN": "全部清除", "zh_TW": "全部清除", "es": "Borrar todo",
            "fr": "Tout effacer", "de": "Alles löschen", "ja": "すべてクリア",
            "pt_BR": "Limpar tudo", "ru": "Очистить всё", "ko": "모두 지우기",
        }),
    ],
    "GraphicView": [
        ("XY", {}),
        ("X only", {
            "zh_CN": "仅X", "zh_TW": "僅X", "es": "Solo X",
            "fr": "X seulement", "de": "Nur X", "ja": "Xのみ",
            "pt_BR": "Somente X", "ru": "Только X", "ko": "X만",
        }),
        ("Y only", {
            "zh_CN": "仅Y", "zh_TW": "僅Y", "es": "Solo Y",
            "fr": "Y seulement", "de": "Nur Y", "ja": "Yのみ",
            "pt_BR": "Somente Y", "ru": "Только Y", "ko": "Y만",
        }),
        ("Polyline", {
            "zh_CN": "折线", "zh_TW": "折線", "es": "Polilínea",
            "fr": "Polyligne", "de": "Polylinie", "ja": "折れ線",
            "pt_BR": "Polilinha", "ru": "Ломаная", "ko": "꺾은선",
        }),
        ("Step", {
            "zh_CN": "阶梯", "zh_TW": "階梯", "es": "Escalón",
            "fr": "Escalier", "de": "Stufe", "ja": "ステップ",
            "pt_BR": "Degrau", "ru": "Ступень", "ko": "계단",
        }),
        ("Points only", {
            "zh_CN": "仅点", "zh_TW": "僅點", "es": "Solo puntos",
            "fr": "Points seuls", "de": "Nur Punkte", "ja": "点のみ",
            "pt_BR": "Somente pontos", "ru": "Только точки", "ko": "점만",
        }),
        ("All colored", {
            "zh_CN": "全部彩色", "zh_TW": "全部彩色", "es": "Todo en color",
            "fr": "Tout en couleur", "de": "Alles farbig", "ja": "すべてカラー",
            "pt_BR": "Tudo colorido", "ru": "Все цветные", "ko": "전체 컬러",
        }),
        ("Selected colored", {
            "zh_CN": "选中彩色", "zh_TW": "選取彩色", "es": "Selección en color",
            "fr": "Sélection en couleur", "de": "Auswahl farbig", "ja": "選択をカラー",
            "pt_BR": "Seleção colorida", "ru": "Выбранные цветные", "ko": "선택 컬러",
        }),
        ("Selected only", {
            "zh_CN": "仅选中", "zh_TW": "僅選取", "es": "Solo selección",
            "fr": "Sélection seule", "de": "Nur Auswahl", "ja": "選択のみ",
            "pt_BR": "Somente seleção", "ru": "Только выбранные", "ko": "선택만",
        }),
        ("Separate", {}),
        ("Overlay · selected", {}),
        ("Overlay · all", {}),
        ("Add Signal", {
            "zh_CN": "添加信号", "zh_TW": "新增訊號", "es": "Añadir señal",
            "fr": "Ajouter un signal", "de": "Signal hinzufügen", "ja": "信号を追加",
            "pt_BR": "Adicionar sinal", "ru": "Добавить сигнал", "ko": "신호 추가",
        }),
        ("Delete Signal", {
            "zh_CN": "删除信号", "zh_TW": "刪除訊號", "es": "Eliminar señal",
            "fr": "Supprimer le signal", "de": "Signal löschen", "ja": "信号を削除",
            "pt_BR": "Excluir sinal", "ru": "Удалить сигнал", "ko": "신호 삭제",
        }),
        ("Pause capture", {
            "zh_CN": "暂停采集", "zh_TW": "暫停擷取", "es": "Pausar captura",
            "fr": "Mettre en pause", "de": "Aufnahme pausieren", "ja": "収集を一時停止",
            "pt_BR": "Pausar captura", "ru": "Пауза захвата", "ko": "캡처 일시정지",
        }),
        ("Resume capture", {
            "zh_CN": "继续采集", "zh_TW": "繼續擷取", "es": "Reanudar captura",
            "fr": "Reprendre", "de": "Aufnahme fortsetzen", "ja": "収集を再開",
            "pt_BR": "Retomar captura", "ru": "Продолжить захват", "ko": "캡처 재개",
        }),
    ],
    "PlaybackTab": [
        ("Playback", {
            "zh_CN": "回放", "zh_TW": "回放", "es": "Reproducción",
            "fr": "Lecture", "de": "Wiedergabe", "ja": "再生",
            "pt_BR": "Reprodução", "ru": "Воспроизведение", "ko": "재생",
        }),
        ("Play", {
            "zh_CN": "播放", "zh_TW": "播放", "es": "Reproducir",
            "fr": "Lecture", "de": "Wiedergeben", "ja": "再生",
            "pt_BR": "Reproduzir", "ru": "Воспроизведение", "ko": "재생",
        }),
        ("Pause", {
            "zh_CN": "暂停", "zh_TW": "暫停", "es": "Pausa",
            "fr": "Pause", "de": "Pause", "ja": "一時停止",
            "pt_BR": "Pausar", "ru": "Пауза", "ko": "일시정지",
        }),
        ("Stop", {
            "zh_CN": "停止", "zh_TW": "停止", "es": "Detener",
            "fr": "Arrêt", "de": "Stopp", "ja": "停止",
            "pt_BR": "Parar", "ru": "Стоп", "ko": "정지",
        }),
        ("Seek:", {
            "zh_CN": "定位:", "zh_TW": "定位:", "es": "Posición:",
            "fr": "Position :", "de": "Position:", "ja": "シーク:",
            "pt_BR": "Posição:", "ru": "Позиция:", "ko": "위치:",
        }),
        ("Speed:", {
            "zh_CN": "速度:", "zh_TW": "速度:", "es": "Velocidad:",
            "fr": "Vitesse :", "de": "Tempo:", "ja": "速度:",
            "pt_BR": "Velocidade:", "ru": "Скорость:", "ko": "속도:",
        }),
        ("Loop", {
            "zh_CN": "循环", "zh_TW": "循環", "es": "Bucle",
            "fr": "Boucle", "de": "Schleife", "ja": "ループ",
            "pt_BR": "Loop", "ru": "Цикл", "ko": "반복",
        }),
        ("File", {
            "zh_CN": "文件", "zh_TW": "檔案", "es": "Archivo",
            "fr": "Fichier", "de": "Datei", "ja": "ファイル",
            "pt_BR": "Arquivo", "ru": "Файл", "ko": "파일",
        }),
        ("Frames", {
            "zh_CN": "帧数", "zh_TW": "幀數", "es": "Tramas",
            "fr": "Trames", "de": "Frames", "ja": "フレーム",
            "pt_BR": "Frames", "ru": "Кадры", "ko": "프레임",
        }),
        ("Duration", {
            "zh_CN": "时长", "zh_TW": "時長", "es": "Duración",
            "fr": "Durée", "de": "Dauer", "ja": "時間",
            "pt_BR": "Duração", "ru": "Длительность", "ko": "길이",
        }),
        ("Progress", {
            "zh_CN": "进度", "zh_TW": "進度", "es": "Progreso",
            "fr": "Progression", "de": "Fortschritt", "ja": "進捗",
            "pt_BR": "Progresso", "ru": "Прогресс", "ko": "진행",
        }),
        ("Status", {
            "zh_CN": "状态", "zh_TW": "狀態", "es": "Estado",
            "fr": "État", "de": "Status", "ja": "状態",
            "pt_BR": "Status", "ru": "Состояние", "ko": "상태",
        }),
        ("Ready", {
            "zh_CN": "就绪", "zh_TW": "就緒", "es": "Listo",
            "fr": "Prêt", "de": "Bereit", "ja": "準備完了",
            "pt_BR": "Pronto", "ru": "Готово", "ko": "준비됨",
        }),
    ],
}

# Flat Python catalog keys (English key == English value for en.json)
PY_STRINGS: dict[str, dict[str, str]] = {
    "Side Bar": {
        "zh_CN": "侧边栏", "zh_TW": "側邊欄", "es": "Barra lateral",
        "fr": "Barre latérale", "de": "Seitenleiste", "ja": "サイドバー",
        "pt_BR": "Barra lateral", "ru": "Боковая панель", "ko": "사이드바",
    },
    "OUTPUT": {},
    "Maximize Editor": {
        "zh_CN": "最大化编辑器", "zh_TW": "最大化編輯器", "es": "Maximizar editor",
        "fr": "Agrandir l'éditeur", "de": "Editor maximieren", "ja": "エディタを最大化",
        "pt_BR": "Maximizar editor", "ru": "Развернуть редактор", "ko": "편집기 최대화",
    },
    "Project": {
        "zh_CN": "工程", "zh_TW": "專案", "es": "Proyecto",
        "fr": "Projet", "de": "Projekt", "ja": "プロジェクト",
        "pt_BR": "Projeto", "ru": "Проект", "ko": "프로젝트",
    },
    "Config": {
        "zh_CN": "配置", "zh_TW": "設定", "es": "Configuración",
        "fr": "Configuration", "de": "Konfiguration", "ja": "設定",
        "pt_BR": "Configuração", "ru": "Конфигурация", "ko": "구성",
    },
    "COM": {},
    "Bus": {
        "zh_CN": "总线", "zh_TW": "匯流排", "es": "Bus",
        "fr": "Bus", "de": "Bus", "ja": "バス",
        "pt_BR": "Barramento", "ru": "Шина", "ko": "버스",
    },
    "Validate": {
        "zh_CN": "校验", "zh_TW": "驗證", "es": "Validar",
        "fr": "Valider", "de": "Prüfen", "ja": "検証",
        "pt_BR": "Validar", "ru": "Проверка", "ko": "검증",
    },
    "Setup": {
        "zh_CN": "设置", "zh_TW": "設定", "es": "Configuración",
        "fr": "Configuration", "de": "Einrichtung", "ja": "セットアップ",
        "pt_BR": "Configuração", "ru": "Настройка", "ko": "설정",
    },
    "Network": {
        "zh_CN": "网络", "zh_TW": "網路", "es": "Red",
        "fr": "Réseau", "de": "Netzwerk", "ja": "ネットワーク",
        "pt_BR": "Rede", "ru": "Сеть", "ko": "네트워크",
    },
    "Device": {
        "zh_CN": "设备", "zh_TW": "裝置", "es": "Dispositivo",
        "fr": "Périphérique", "de": "Gerät", "ja": "デバイス",
        "pt_BR": "Dispositivo", "ru": "Устройство", "ko": "장치",
    },
    "EDS": {},
    "Library": {
        "zh_CN": "库", "zh_TW": "程式庫", "es": "Biblioteca",
        "fr": "Bibliothèque", "de": "Bibliothek", "ja": "ライブラリ",
        "pt_BR": "Biblioteca", "ru": "Библиотека", "ko": "라이브러리",
    },
    "AUTOSAR Studio": {},
    "CANopen Suite": {},
    "Pause": {
        "zh_CN": "暂停", "zh_TW": "暫停", "es": "Pausa",
        "fr": "Pause", "de": "Pause", "ja": "一時停止",
        "pt_BR": "Pausar", "ru": "Пауза", "ko": "일시정지",
    },
    "Export": {
        "zh_CN": "导出", "zh_TW": "匯出", "es": "Exportar",
        "fr": "Exporter", "de": "Exportieren", "ja": "エクスポート",
        "pt_BR": "Exportar", "ru": "Экспорт", "ko": "내보내기",
    },
    "Clear": {
        "zh_CN": "清空", "zh_TW": "清除", "es": "Borrar",
        "fr": "Effacer", "de": "Leeren", "ja": "クリア",
        "pt_BR": "Limpar", "ru": "Очистить", "ko": "지우기",
    },
    "Toggle Side Bar (Ctrl+B)": {
        "zh_CN": "切换侧边栏 (Ctrl+B)", "zh_TW": "切換側邊欄 (Ctrl+B)",
        "es": "Barra lateral (Ctrl+B)", "fr": "Barre latérale (Ctrl+B)",
        "de": "Seitenleiste (Ctrl+B)", "ja": "サイドバー (Ctrl+B)",
        "pt_BR": "Barra lateral (Ctrl+B)", "ru": "Боковая панель (Ctrl+B)",
        "ko": "사이드바 (Ctrl+B)",
    },
    "Toggle Activity Bar (Ctrl+B)": {
        "zh_CN": "切换活动栏 (Ctrl+B)", "zh_TW": "切換活動列 (Ctrl+B)",
        "es": "Barra de actividad (Ctrl+B)", "fr": "Barre d'activités (Ctrl+B)",
        "de": "Aktivitätsleiste (Ctrl+B)", "ja": "アクティビティバー (Ctrl+B)",
        "pt_BR": "Barra de atividades (Ctrl+B)", "ru": "Панель действий (Ctrl+B)",
        "ko": "활동 표시줄 (Ctrl+B)",
    },
    "Toggle Panel (Ctrl+J)": {
        "zh_CN": "切换面板 (Ctrl+J)", "zh_TW": "切換面板 (Ctrl+J)",
        "es": "Panel (Ctrl+J)", "fr": "Panneau (Ctrl+J)",
        "de": "Panel (Ctrl+J)", "ja": "パネル (Ctrl+J)",
        "pt_BR": "Painel (Ctrl+J)", "ru": "Панель (Ctrl+J)",
        "ko": "패널 (Ctrl+J)",
    },
    "Maximize Editor — hide side bar & panel": {
        "zh_CN": "最大化编辑器 — 隐藏侧边栏和面板",
        "zh_TW": "最大化編輯器 — 隱藏側邊欄與面板",
        "es": "Maximizar editor", "fr": "Agrandir l'éditeur",
        "de": "Editor maximieren", "ja": "エディタを最大化",
        "pt_BR": "Maximizar editor", "ru": "Развернуть редактор",
        "ko": "편집기 최대화",
    },
    "Maximize Editor — hide activity bar & panel": {
        "zh_CN": "最大化编辑器 — 隐藏活动栏和面板",
        "zh_TW": "最大化編輯器 — 隱藏活動列與面板",
        "es": "Maximizar editor", "fr": "Agrandir l'éditeur",
        "de": "Editor maximieren", "ja": "エディタを最大化",
        "pt_BR": "Maximizar editor", "ru": "Развернуть редактор",
        "ko": "편집기 최대화",
    },
    "OUTPUT — double-click to collapse (Ctrl+J)": {
        "zh_CN": "OUTPUT — 双击折叠 (Ctrl+J)",
        "zh_TW": "OUTPUT — 連按兩下摺疊 (Ctrl+J)",
        "es": "OUTPUT — doble clic para plegar (Ctrl+J)",
        "fr": "OUTPUT — double-clic pour réduire (Ctrl+J)",
        "de": "OUTPUT — Doppelklick zum Einklappen (Ctrl+J)",
        "ja": "OUTPUT — ダブルクリックで折りたたむ (Ctrl+J)",
        "pt_BR": "OUTPUT — clique duplo para recolher (Ctrl+J)",
        "ru": "OUTPUT — двойной щелчок свернуть (Ctrl+J)",
        "ko": "OUTPUT — 더블클릭으로 접기 (Ctrl+J)",
    },
}


def escape_xml(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def write_ts(locale: str) -> None:
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        "<!DOCTYPE TS>",
        f'<TS version="2.1" language="{locale}">',
    ]
    for ctx, messages in STRINGS.items():
        lines.append(f"<context>")
        lines.append(f"    <name>{escape_xml(ctx)}</name>")
        for source, trans_map in messages:
            tr = trans_map.get(locale, "")
            lines.append("    <message>")
            lines.append(f"        <source>{escape_xml(source)}</source>")
            if tr:
                lines.append(f"        <translation>{escape_xml(tr)}</translation>")
            else:
                lines.append('        <translation type="unfinished"></translation>')
            lines.append("    </message>")
        lines.append("</context>")
    lines.append("</TS>")
    lines.append("")
    path = TS_DIR / f"openbus_{locale}.ts"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {path.relative_to(ROOT)}")


def write_json_locales() -> None:
    PY_DIR.mkdir(parents=True, exist_ok=True)
    # en: identity map
    en = {k: k for k in PY_STRINGS}
    (PY_DIR / "en.json").write_text(
        json.dumps(en, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print("wrote plugins/_shared/locales/en.json")
    for loc in LOCALES + ["en"]:
        if loc == "en":
            continue
        data = {}
        for key, m in PY_STRINGS.items():
            data[key] = m.get(loc) or key
        (PY_DIR / f"{loc}.json").write_text(
            json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print(f"wrote plugins/_shared/locales/{loc}.json")


def main() -> None:
    TS_DIR.mkdir(parents=True, exist_ok=True)
    for loc in LOCALES:
        write_ts(loc)
    write_json_locales()
    readme = TS_DIR / "README.md"
    readme.write_text(
        "# openbus UI translations\n\n"
        "- Source language in C++/Python: **English** (`tr(\"...\")` / `t(\"...\")`).\n"
        "- Non-English catalogs: `openbus_<locale>.ts` → build produces `.qm` beside the exe.\n"
        "- Regenerate stubs / refresh catalogs: `python scripts/generate_i18n_catalogs.py`\n"
        "- After adding new `tr()` strings, prefer `lupdate` (or extend the generator) then rebuild.\n"
        "- Python plugin strings: `plugins/_shared/locales/<locale>.json`\n",
        encoding="utf-8",
    )
    print("done")


if __name__ == "__main__":
    main()
