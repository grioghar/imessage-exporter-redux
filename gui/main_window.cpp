#include "main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextDocument>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <ctime>
#include <string>

#include "imsg/backup.hpp"
#include "imsg/contact_store.hpp"
#include "imsg/contacts.hpp"
#include "imsg/database.hpp"
#include "imsg/exporters.hpp"
#include "imsg/log.hpp"
#include "imsg/build_stamp.hpp"
#include "imsg/theme.hpp"
#include "imsg/time_util.hpp"
#include "imsg/vcard.hpp"
#include "imsg/version.hpp"
#include "google_auth.hpp"
#include "link_preview.hpp"
#include "secret_store.hpp"

namespace {
// Index conventions for the source / contacts combo boxes.
enum Source { SrcAuto = 0, SrcFile = 1, SrcBackup = 2 };
enum Contacts { CtNone = 0, CtThisMac = 1, CtFile = 2, CtBackup = 3, CtStore = 4 };

// Modal dialog showing rich text with clickable (externally-opened) links.
void richTextDialog(QWidget* parent, const QString& title, const QString& html) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dlg);
    auto* label = new QLabel(html, &dlg);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(label);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    dlg.resize(580, 460);
    dlg.exec();
}

// Decodes a "data:<mime>;base64,<data>" URI into a small QIcon, or a null icon
// when the URI isn't inline image data (e.g. an https URL we won't fetch here).
QIcon iconFromDataUri(const QString& uri) {
    const int comma = uri.indexOf("base64,");
    if (!uri.startsWith("data:image", Qt::CaseInsensitive) || comma < 0) return QIcon();
    const QByteArray bytes = QByteArray::fromBase64(uri.mid(comma + 7).toLatin1());
    QPixmap pm;
    if (bytes.isEmpty() || !pm.loadFromData(bytes)) return QIcon();
    return QIcon(pm.scaled(28, 28, Qt::KeepAspectRatioByExpanding,
                           Qt::SmoothTransformation));
}
}  // namespace

MainWindow::MainWindow()
    : logMutex_(std::make_shared<std::mutex>()),
      logBuffer_(std::make_shared<std::vector<std::string>>()) {
    setWindowTitle("iMessage Exporter");

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    logFilePath_ = QDir(dataDir).filePath("imessage-exporter.log");

    // ============ Tabbed per-export workflow ============
    tabs_ = new QTabWidget;

    // --- Source tab ---------------------------------------------------------
    auto* sourcePage = new QWidget;
    auto* srcForm = new QFormLayout(sourcePage);
    source_ = new QComboBox;
#if defined(Q_OS_MACOS)
    // Auto-detecting ~/Library/Messages/chat.db only makes sense on a Mac.
    source_->addItem("Auto-detect Messages on this Mac", SrcAuto);
#endif
    source_->addItem("Database file (chat.db / sms.db)…", SrcFile);
    source_->addItem("Device backup…", SrcBackup);
    srcForm->addRow("Source:", source_);

    auto* dbRow = new QHBoxLayout;
    dbPath_ = new QLineEdit;
    dbPath_->setPlaceholderText("Path to a Messages database");
    dbBrowse_ = new QPushButton("Browse…");
    dbRow->addWidget(dbPath_);
    dbRow->addWidget(dbBrowse_);
    srcForm->addRow("Database:", dbRow);

    backup_ = new QComboBox;
    for (const imsg::BackupInfo& b : imsg::list_backups()) {
        QString label = QString::fromStdString(b.udid) +
                        (b.encrypted ? "  [encrypted]" : "");
        backup_->addItem(label, QString::fromStdString(b.path));
    }
    if (backup_->count() == 0) backup_->addItem("(no backups found)", QString());
    srcForm->addRow("Backup:", backup_);

    copyDbBtn_ = new QPushButton("Copy Messages data to a local cache…");
    copyDbBtn_->setToolTip("Copy chat.db to a folder this app can always read, so "
                           "you don't need Full Disk Access on every run. (The copy "
                           "itself needs read access once.)");
    srcForm->addRow("", copyDbBtn_);
    tabs_->addTab(sourcePage, "Source");

    // --- Filters tab --------------------------------------------------------
    auto* filtersPage = new QWidget;
    auto* filForm = new QFormLayout(filtersPage);
    // The date fields themselves arm the filter — no separate checkboxes (which
    // confused people). "From" left at its "(any start)" sentinel = no lower
    // bound; "To" left at today = through now. Picking a real date turns that
    // bound on. The live summary below always says exactly what will be exported.
    auto* sinceRow = new QHBoxLayout;
    since_ = new QDateEdit;
    since_->setDisplayFormat("yyyy-MM-dd");
    since_->setCalendarPopup(true);
    since_->setMinimumDate(QDate(2001, 1, 1));         // Apple-time epoch start
    since_->setMaximumDate(QDate::currentDate());
    since_->setSpecialValueText("(any start)");        // shown at minimumDate
    since_->setDate(since_->minimumDate());            // default: no lower bound
    since_->setToolTip("Only export messages on/after this date. Leave it at "
                       "\"(any start)\" for no start limit.");
    until_ = new QDateEdit;
    until_->setDisplayFormat("yyyy-MM-dd");
    until_->setCalendarPopup(true);
    until_->setMinimumDate(QDate(2001, 1, 1));
    until_->setMaximumDate(QDate::currentDate());
    until_->setDate(until_->maximumDate());            // default: today = through now
    until_->setToolTip("Only export messages on/before this date. Leave it at "
                       "today for no end limit.");
    sinceRow->addWidget(new QLabel("From:"));
    sinceRow->addWidget(since_);
    sinceRow->addWidget(new QLabel("To:"));
    sinceRow->addWidget(until_);
    sinceRow->addStretch();
    filForm->addRow("Date range:", sinceRow);
    dateSummary_ = new QLabel;
    dateSummary_->setTextFormat(Qt::RichText);
    dateSummary_->setWordWrap(true);
    filForm->addRow("", dateSummary_);

    auto* peopleRow = new QHBoxLayout;
    peopleBtn_ = new QPushButton("Select people…");
    peopleBtn_->setToolTip("Export only conversations with specific people.");
    peopleLabel_ = new QLabel("All conversations");
    peopleLabel_->setStyleSheet("color:#6e6e73");
    peopleRow->addWidget(peopleBtn_);
    peopleRow->addWidget(peopleLabel_);
    peopleRow->addStretch();
    filForm->addRow("People:", peopleRow);
    tabs_->addTab(filtersPage, "Filters");

    // --- Output tab ---------------------------------------------------------
    auto* outputPage = new QWidget;
    auto* outForm = new QFormLayout(outputPage);
    format_ = new QComboBox;
    format_->addItems({"txt", "md", "html", "json", "pdf", "android"});
    format_->setCurrentText("html");
    outForm->addRow("Format:", format_);

    // HTML/PDF visual theme; built-ins come from the core so the list stays in
    // one place. Ignored by the engine for non-HTML formats.
    themeCombo_ = new QComboBox;
    for (const std::string& t : imsg::theme_names())
        themeCombo_->addItem(QString::fromStdString(t));
    themeCombo_->setCurrentText("ios");
    outForm->addRow("Theme:", themeCombo_);

    auto* outRow = new QHBoxLayout;
    outputDir_ = new QLineEdit;
    outputDir_->setText(QDir::homePath() + "/iMessage Export");
    auto* outBrowse = new QPushButton("Browse…");
    outRow->addWidget(outputDir_);
    outRow->addWidget(outBrowse);
    outForm->addRow("Output folder:", outRow);

    meLabel_ = new QLineEdit("Me");
    outForm->addRow("Your name:", meLabel_);

    combined_ = new QCheckBox("Single combined file (instead of one per conversation)");
    outForm->addRow("", combined_);
    statsCover_ = new QCheckBox("Add a statistics cover page");
    outForm->addRow("", statsCover_);
    tabs_->addTab(outputPage, "Output");

    // --- Run tab ------------------------------------------------------------
    auto* runPage = new QWidget;
    auto* runLayout = new QVBoxLayout(runPage);
    exportBtn_ = new QPushButton("Export");
    pauseBtn_ = new QPushButton("Pause");
    pauseBtn_->setEnabled(false);
    stopBtn_ = new QPushButton("Stop");
    stopBtn_->setEnabled(false);
    openBtn_ = new QPushButton("Open output folder");
    openBtn_->setEnabled(false);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(exportBtn_);
    btnRow->addWidget(pauseBtn_);
    btnRow->addWidget(stopBtn_);
    btnRow->addWidget(openBtn_);
    btnRow->addStretch();
    runLayout->addLayout(btnRow);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMinimumHeight(220);
    runLayout->addWidget(logView_);
    runTabIndex_ = tabs_->addTab(runPage, "Run");

    // ============ Preferences pane (tabbed; persistent configuration) ============
    // Cloud Accounts comes first, then the contacts source, options and logging.
    // These controls hold state via QSettings (load/saveSettings).
    prefsDialog_ = new QDialog(this);
    prefsDialog_->setWindowTitle("Preferences");
    prefsDialog_->resize(540, 380);
    auto* prefLayout = new QVBoxLayout(prefsDialog_);
    prefsTabs_ = new QTabWidget;
    prefLayout->addWidget(prefsTabs_);

    // -- Cloud Accounts tab (first) --
    auto* cloudPage = new QWidget;
    auto* cloudForm = new QFormLayout(cloudPage);
    icloudBtn_ = new QPushButton("Import iCloud Contacts…");
    icloudBtn_->setToolTip("Fetch your iCloud contacts over CardDAV using an "
                           "app-specific password.");
    cloudForm->addRow("iCloud:", icloudBtn_);
    googleBtn_ = new QPushButton("Connect Google Contacts…");
    googleBtn_->setToolTip("Sign in to Google and download your contacts (enter or "
                           "import a Google OAuth client JSON).");
    cloudForm->addRow("Google Contacts:", googleBtn_);
    driveBtn_ = new QPushButton;  // label set in the GoogleDrive wiring below
    driveBtn_->setToolTip("Authorize Google Drive once; the sign-in is saved so "
                          "future exports can upload automatically.");
    cloudForm->addRow("Google Drive:", driveBtn_);
    uploadDrive_ = new QCheckBox("Upload export to Drive when finished");
    cloudForm->addRow("", uploadDrive_);
    driveFolder_ = new QLineEdit;
    driveFolder_->setPlaceholderText("Drive folder name (e.g. iMessage Export)");
    cloudForm->addRow("Drive folder:", driveFolder_);
    prefsTabs_->addTab(cloudPage, "Cloud Accounts");

    // -- Contacts tab --
    auto* contactsPage = new QWidget;
    auto* cForm = new QFormLayout(contactsPage);
    contacts_ = new QComboBox;
    contacts_->addItem("None (show phone numbers / emails)");
    contacts_->addItem("This Mac's Contacts");
    contacts_->addItem("Contacts file (.abcddb / .vcf)…");
    contacts_->addItem("From the selected backup");
    contacts_->addItem("Saved contacts database (Google / imported)");
    cForm->addRow("Names:", contacts_);
    auto* ctRow = new QHBoxLayout;
    contactsPath_ = new QLineEdit;
    contactsPath_->setPlaceholderText("Path to .abcddb or .vcf");
    contactsBrowse_ = new QPushButton("Browse…");
    ctRow->addWidget(contactsPath_);
    ctRow->addWidget(contactsBrowse_);
    cForm->addRow("Contacts file:", ctRow);
    prefsTabs_->addTab(contactsPage, "Contacts");

    // -- Export options tab --
    auto* optsPage = new QWidget;
    auto* oForm = new QFormLayout(optsPage);
    copyAttachments_ = new QCheckBox("Copy attachment files (needed to show pictures)");
    copyAttachments_->setChecked(true);  // so images/movies show by default
    copyAttachments_->setToolTip("Copy each conversation's pictures, movies and "
                                 "files next to the export so they display inline. "
                                 "On macOS, HEIC photos are converted to JPEG.");
    embedAttachments_ = new QCheckBox("Embed attachments (larger files)");
    embedAttachments_->setToolTip("Inline pictures, movies and files as base64 so "
                                  "each HTML/JSON file is self-contained.");
    hiddenAttachDir_ = new QCheckBox("Hidden attachments folder");
    hiddenAttachDir_->setToolTip("Name each conversation's attachments folder with "
                                 "a leading dot (hidden on macOS/Linux).");
    richPreviews_ = new QCheckBox("Rich link previews (online)");
    richPreviews_->setToolTip(
        "For HTML/PDF exports, fetch each shared link's Open Graph preview "
        "(hero image, title, description) over the network and embed it inline, "
        "like Messages shows. Slower and requires internet; without it, links "
        "still get a favicon + site card. YouTube/Spotify/Vimeo always embed.");
    auto* attCol = new QVBoxLayout;
    attCol->addWidget(copyAttachments_);
    attCol->addWidget(embedAttachments_);
    attCol->addWidget(hiddenAttachDir_);
    attCol->addWidget(richPreviews_);
    oForm->addRow("Attachments:", attCol);
    prefsTabs_->addTab(optsPage, "Export options");

    // -- Logging tab --
    auto* logPage = new QWidget;
    auto* lForm = new QFormLayout(logPage);
    logLevel_ = new QComboBox;
    logLevel_->addItems({"error", "warn", "info", "debug"});
    logLevel_->setCurrentText("info");
    lForm->addRow("Log level:", logLevel_);
    prefsTabs_->addTab(logPage, "Logging");

    auto* prefButtons = new QDialogButtonBox(QDialogButtonBox::Close, prefsDialog_);
    connect(prefButtons, &QDialogButtonBox::rejected, prefsDialog_, [this] {
        saveSettings();
        prefsDialog_->hide();
    });
    prefLayout->addWidget(prefButtons);

    // --- Menu bar (Help + Settings) -----------------------------------------
    auto* menuBar = new QMenuBar;
    QMenu* settingsMenu = menuBar->addMenu("&Settings");
    QAction* prefsAction = new QAction("Preferences…", this);
    prefsAction->setShortcut(QKeySequence::Preferences);
    prefsAction->setMenuRole(QAction::PreferencesRole);  // → app menu on macOS
    connect(prefsAction, &QAction::triggered, this, &MainWindow::showPreferences);
    settingsMenu->addAction(prefsAction);

    QMenu* helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("How to get your data…", this, &MainWindow::showHowToGetData);
#ifdef Q_OS_MACOS
    helpMenu->addAction("Fix Full Disk Access…", this,
                        &MainWindow::showFullDiskAccessHelp);
#endif
    helpMenu->addAction("Google Contacts setup…", this, &MainWindow::showGoogleSetup);
    helpMenu->addAction("Online documentation", this, [] {
        QDesktopServices::openUrl(
            QUrl("https://github.com/grioghar/imessage-exporter-redux#readme"));
    });
    helpMenu->addAction("Report an issue", this, [] {
        QDesktopServices::openUrl(
            QUrl("https://github.com/grioghar/imessage-exporter-redux/issues"));
    });
    helpMenu->addSeparator();
    QAction* autoUpdate = helpMenu->addAction("Automatically check for updates");
    autoUpdate->setCheckable(true);
    autoUpdate->setChecked(QSettings().value("updates/autoCheck", true).toBool());
    connect(autoUpdate, &QAction::toggled, this,
            [](bool on) { QSettings().setValue("updates/autoCheck", on); });
    helpMenu->addAction("Check for updates now", this,
                        [this] { runUpdateCheck(true); });
    helpMenu->addSeparator();
    helpMenu->addAction("About iMessage Exporter", this, &MainWindow::showAbout);

    auto* root = new QVBoxLayout(this);
    root->setMenuBar(menuBar);
    root->addWidget(tabs_);

    // Persistent status bar at the bottom, visible from every tab: a status
    // message on the left and an export progress bar on the right.
    status_ = new QLabel("Ready.");
    progress_ = new QProgressBar;
    progress_->setMinimumWidth(240);
    progress_->setTextVisible(true);
    progress_->setFormat("%p%");  // show the percentage done
    progress_->setVisible(false);
    statusBar_ = new QStatusBar;
    statusBar_->setSizeGripEnabled(false);
    statusBar_->addWidget(status_, 1);
    statusBar_->addPermanentWidget(progress_);
    root->addWidget(statusBar_);

    connect(exportBtn_, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(pauseBtn_, &QPushButton::clicked, this, &MainWindow::pauseExport);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::stopExport);
    connect(source_, &QComboBox::currentIndexChanged, this, &MainWindow::onSourceChanged);
    connect(contacts_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onContactsChanged);
    connect(dbBrowse_, &QPushButton::clicked, this, &MainWindow::browseDatabase);
    connect(outBrowse, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(contactsBrowse_, &QPushButton::clicked, this, &MainWindow::browseContacts);
    // Picking a date arms that bound; the summary reflects it live.
    connect(since_, &QDateEdit::dateChanged, this, &MainWindow::updateDateSummary);
    connect(until_, &QDateEdit::dateChanged, this, &MainWindow::updateDateSummary);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::openOutputDir);
    connect(icloudBtn_, &QPushButton::clicked, this,
            &MainWindow::importICloudContacts);
    connect(peopleBtn_, &QPushButton::clicked, this, &MainWindow::pickPeople);
    connect(copyDbBtn_, &QPushButton::clicked, this, &MainWindow::copyMessagesData);
    connect(&watcher_, &QFutureWatcher<imsg::ExportSummary>::finished, this,
            &MainWindow::exportFinished);
    connect(&icloudWatcher_, &QFutureWatcher<icloud::Result>::finished, this,
            &MainWindow::icloudFinished);

    // --- Auto-update ---------------------------------------------------------
    updater_ = new Updater(this);
    connect(updater_, &Updater::updateAvailable, this,
            [this](const QString& version, const QString&, QUrl asset, QString) {
                status_->setText("Downloading update " + version + "…");
                updater_->download(asset, version);
            });
    connect(updater_, &Updater::updateDownloaded, this,
            [this](const QString& path, const QString& version) {
                if (QMessageBox::question(
                        this, "Update ready",
                        "Version " + version +
                            " has been downloaded.\n\nInstall it and restart now?") !=
                    QMessageBox::Yes) {
                    status_->setText("Update " + version + " is ready to install.");
                    return;
                }
                if (updater_->installAndRestart(path))
                    QApplication::quit();
                else
                    QMessageBox::information(
                        this, "Update",
                        "The update was opened. Finish installing it (or update via "
                        "your package manager), then reopen the app.");
            });
    connect(updater_, &Updater::upToDate, this, [this] {
        if (manualUpdateCheck_)
            QMessageBox::information(this, "Up to date",
                                     "You already have the latest version.");
    });
    connect(updater_, &Updater::failed, this, [this](const QString& error) {
        if (manualUpdateCheck_)
            QMessageBox::warning(this, "Update check failed", error);
        else
            status_->setText("Update check failed.");
    });
    if (QSettings().value("updates/autoCheck", true).toBool())
        QTimer::singleShot(1500, this, [this] { runUpdateCheck(false); });

    // --- Google Contacts -----------------------------------------------------
    google_ = new GoogleContacts(this);
    connect(google_, &GoogleContacts::status, this,
            [this](const QString& m) { status_->setText(m); });
    connect(google_, &GoogleContacts::finished, this, [this](int n) {
        contacts_->setCurrentIndex(CtStore);
        onContactsChanged();
        status_->setText(QString("Imported %1 Google contact entr%2 into the saved "
                                 "database.")
                             .arg(n)
                             .arg(n == 1 ? "y" : "ies"));
        QMessageBox::information(
            this, "Google Contacts",
            QString("Downloaded %1 entries. They are saved and will be used for "
                    "name resolution (\"Saved contacts database\").")
                .arg(n));
    });
    connect(google_, &GoogleContacts::failed, this, [this](const QString& e) {
        status_->setText("Google import failed.");
        QMessageBox::warning(this, "Google Contacts", e);
    });
    connect(googleBtn_, &QPushButton::clicked, this, &MainWindow::connectGoogle);

    drive_ = new GoogleDrive(this);
    driveBtn_->setText(GoogleDrive::isConnected() ? "Reconnect Google Drive…"
                                                  : "Connect Google Drive…");
    connect(drive_, &GoogleDrive::status, this,
            [this](const QString& m) { status_->setText(m); });
    connect(drive_, &GoogleDrive::connected, this, [this] {
        driveBtn_->setText("Reconnect Google Drive…");
        uploadDrive_->setChecked(true);
        status_->setText("Google Drive connected. Exports can now upload automatically.");
        QMessageBox::information(this, "Google Drive",
                                 "Google Drive is connected. Tick \"Upload export to "
                                 "Drive when finished\" to push exports to your chosen "
                                 "folder.");
    });
    connect(drive_, &GoogleDrive::failed, this, [this](const QString& e) {
        status_->setText("Google Drive connection failed.");
        QMessageBox::warning(this, "Google Drive", e);
    });
    connect(driveBtn_, &QPushButton::clicked, this, &MainWindow::connectDrive);
    connect(&driveWatcher_, &QFutureWatcher<drive::UploadResult>::finished, this,
            &MainWindow::driveUploadFinished);

    onSourceChanged();
    onContactsChanged();
    loadSettings();
    updateDateSummary();  // ensure the summary shows on first run too
    QTimer::singleShot(0, this, [this] { maybeResumePrevious(); });
}

void MainWindow::onSourceChanged() {
    const int s = source_->currentData().toInt();
    const bool file = (s == SrcFile);
    const bool backup = (s == SrcBackup);
    dbPath_->setEnabled(file);
    dbBrowse_->setEnabled(true);  // always browsable; picking a file switches mode
    backup_->setEnabled(backup);
}

void MainWindow::onContactsChanged() {
    const bool file = (contacts_->currentIndex() == CtFile);
    contactsPath_->setEnabled(file);
    contactsBrowse_->setEnabled(file);
}

void MainWindow::browseDatabase() {
    // Start in the Messages folder so chat.db is easy to find, and show hidden
    // entries so ~/Library is navigable. On macOS use Qt's own dialog: the native
    // panel can fail to browse the TCC-protected Messages folder, and (for some
    // quarantined builds) not appear at all.
    QFileDialog dlg(this, "Choose a Messages database (chat.db / sms.db)");
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("Databases (*.db *.sqlite *.sqlitedb);;All files (*)");
    dlg.setFilter(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    QString start = QDir::homePath() + "/Library/Messages";
    if (!QFileInfo::exists(start)) start = QDir::homePath();
    dlg.setDirectory(start);
#ifdef Q_OS_MACOS
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return;
    dbPath_->setText(dlg.selectedFiles().first());
    const int idx = source_->findData(SrcFile);  // auto-switch Source to "Database file"
    if (idx >= 0) source_->setCurrentIndex(idx);
    onSourceChanged();
}

void MainWindow::browseOutput() {
    QString d = QFileDialog::getExistingDirectory(this, "Choose an output folder",
                                                  outputDir_->text());
    if (!d.isEmpty()) outputDir_->setText(d);
}

void MainWindow::browseContacts() {
    QString f = QFileDialog::getOpenFileName(
        this, "Choose a contacts file", QDir::homePath(),
        "Contacts (*.abcddb *.vcf);;All files (*)");
    if (!f.isEmpty()) contactsPath_->setText(f);
}

bool MainWindow::buildInputs(std::string& db_path, std::string& out_dir,
                             imsg::Format& fmt, imsg::ExportOptions& opts,
                             QString& error) {
    out_dir = outputDir_->text().toStdString();
    if (out_dir.empty()) {
        error = "Please choose an output folder.";
        return false;
    }
    // PDF is produced by converting HTML output (Qt), so ask the engine for HTML.
    if (format_->currentText() == "pdf") {
        fmt = imsg::Format::Html;
    } else if (!imsg::parse_format(format_->currentText().toStdString(), fmt)) {
        error = "Unknown format.";
        return false;
    }

    opts.me_label = meLabel_->text().isEmpty() ? "Me" : meLabel_->text().toStdString();
    opts.html_theme = themeCombo_->currentText().toStdString();
    opts.combined = combined_->isChecked();
    opts.stats_cover = statsCover_->isChecked();
    opts.copy_attachments = copyAttachments_->isChecked();
    opts.embed_attachments = embedAttachments_->isChecked();
    opts.hidden_attachment_dir = hiddenAttachDir_->isChecked();

    // A start date later than the "(any start)" sentinel arms the lower bound;
    // an end date earlier than today arms the upper bound.
    if (since_->date() > since_->minimumDate() &&
        imsg::parse_date(since_->date().toString("yyyy-MM-dd").toStdString(), opts.since))
        opts.has_since = true;
    if (until_->date() < QDate::currentDate() &&
        imsg::parse_date(until_->date().toString("yyyy-MM-dd").toStdString(),
                         opts.until, /*end_of_day=*/true))
        opts.has_until = true;

    const int src = source_->currentData().toInt();
    std::string backup_dir;
    if (src == SrcAuto) {
        db_path = imsg::default_db_path();
        if (!QFile::exists(QString::fromStdString(db_path))) {
            error = "No Messages database was found on this Mac. Enable \"Messages "
                    "in iCloud\" and grant Full Disk Access, or pick a file/backup.";
            return false;
        }
    } else if (src == SrcFile) {
        db_path = dbPath_->text().toStdString();
        if (db_path.empty() || !QFile::exists(dbPath_->text())) {
            error = "Please choose a valid database file.";
            return false;
        }
    } else {  // SrcBackup
        backup_dir = backup_->currentData().toString().toStdString();
        if (backup_dir.empty()) {
            error = "No device backup is available to export from.";
            return false;
        }
        imsg::BackupInfo info = imsg::inspect_backup(backup_dir);
        if (info.encrypted) {
            error = "That backup is encrypted, which isn't supported yet. Turn off "
                    "\"Encrypt local backup\" in Finder and back up again.";
            return false;
        }
        if (!tempDir_.isValid()) {
            error = "Could not create a temporary working directory.";
            return false;
        }
        std::string sms = tempDir_.filePath("sms.db").toStdString();
        std::string err;
        if (!imsg::extract_backup_file(backup_dir, imsg::kMessagesDomain,
                                       imsg::kMessagesRelativePath, sms, err)) {
            error = QString::fromStdString("Could not read messages from backup: " + err);
            return false;
        }
        db_path = sms;
    }

    // Contacts.
    switch (contacts_->currentIndex()) {
        case CtThisMac:
            opts.use_contacts = true;
            break;
        case CtFile:
            opts.contacts_path = contactsPath_->text().toStdString();
            if (opts.contacts_path.empty()) {
                error = "Please choose a contacts file, or pick a different Names option.";
                return false;
            }
            break;
        case CtBackup: {
            if (backup_dir.empty()) {
                error = "\"Names from the selected backup\" needs a Device backup source.";
                return false;
            }
            std::string ab = tempDir_.filePath("AddressBook.sqlitedb").toStdString();
            std::string err;
            if (imsg::extract_backup_file(backup_dir, imsg::kContactsDomain,
                                          imsg::kContactsRelativePath, ab, err))
                opts.contacts_path = ab;
            break;
        }
        case CtStore:
            opts.use_contact_store = true;
            break;
        default:
            break;
    }
    // Always merge the saved store (downloaded Google + iCloud contacts) on top
    // of whatever source is selected, so the address books are used together
    // rather than either/or. (No-op when the store is empty.)
    if (contacts_->currentIndex() != CtNone) opts.use_contact_store = true;

    for (const QString& p : selectedPeople_)
        opts.only_participants.push_back(p.toStdString());
    return true;
}

void MainWindow::setBusy(bool busy) {
    exportBtn_->setEnabled(!busy);
    pauseBtn_->setEnabled(busy);
    stopBtn_->setEnabled(busy);
    if (!busy) pauseBtn_->setText("Pause");
    if (progress_) {
        progress_->setVisible(busy);
        if (busy) progress_->setRange(0, 0);  // indeterminate until totals arrive
    }
    status_->setText(busy ? "Exporting…" : "Ready.");
}

void MainWindow::showPreferences() {
    prefsDialog_->show();
    prefsDialog_->raise();
    prefsDialog_->activateWindow();
}

void MainWindow::updateDateSummary() {
    if (!dateSummary_) return;
    const bool hasSince = since_->date() > since_->minimumDate();
    const bool hasUntil = until_->date() < QDate::currentDate();
    const QString from = since_->date().toString("yyyy-MM-dd");
    const QString to = until_->date().toString("yyyy-MM-dd");
    QString text;
    bool active = true;
    if (!hasSince && !hasUntil) {
        text = "Exporting <b>all messages</b> (no date limit).";
        active = false;
    } else if (hasSince && !hasUntil) {
        text = "Exporting messages <b>from " + from + " through today</b>.";
    } else if (!hasSince && hasUntil) {
        text = "Exporting messages <b>up to " + to + "</b>.";
    } else {
        text = "Exporting messages <b>from " + from + " to " + to + "</b>.";
    }
    dateSummary_->setText(text);
    dateSummary_->setStyleSheet(active ? "color:#0b7d2b" : "color:#6e6e73");
}

void MainWindow::pauseExport() {
    if (!jobRunning_) return;
    const bool nowPaused = !paused_.load();
    paused_.store(nowPaused);
    pauseBtn_->setText(nowPaused ? "Resume" : "Pause");
    status_->setText(nowPaused ? "Paused — will stop after the current conversation."
                               : "Resuming…");
}

void MainWindow::stopExport() {
    if (!jobRunning_) return;
    stopRequested_.store(true);
    paused_.store(false);  // unblock a paused worker so it can see the stop
    pauseBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    status_->setText("Stopping after the current conversation…");
}

void MainWindow::startExport() { startExportResuming(false); }

void MainWindow::startExportResuming(bool resume) {
    std::string db_path, out_dir;
    imsg::Format fmt = imsg::Format::Text;
    imsg::ExportOptions opts;
    QString error;
    if (!buildInputs(db_path, out_dir, fmt, opts, error)) {
        QMessageBox::warning(this, "Cannot export", error);
        return;
    }
    resuming_ = resume;
    opts.skip_existing = resume;  // resume: don't redo finished conversation files
    if (tabs_ && runTabIndex_ >= 0) tabs_->setCurrentIndex(runTabIndex_);  // show progress

    // PDF: write HTML to a temp dir now; convert to PDF in exportFinished().
    wantPdf_ = (format_->currentText() == "pdf");
    if (wantPdf_) {
        pdfRealOut_ = QString::fromStdString(out_dir);
        pdfHtmlDir_ = tempDir_.isValid() ? tempDir_.filePath("pdf-html")
                                         : QDir::tempPath() + "/imsg-pdf-html";
        QDir().mkpath(pdfHtmlDir_);
        out_dir = pdfHtmlDir_.toStdString();
    }

    // Pictures/movies only render inline when their bytes are alongside the
    // export. For the visual formats, copy attachments unless the user is
    // embedding them — otherwise images would degrade to bare links.
    const QString fmtName = format_->currentText();
    if ((fmtName == "html" || fmtName == "pdf") && !opts.embed_attachments)
        opts.copy_attachments = true;

    imsg::LogLevel lvl;
    if (imsg::parse_log_level(logLevel_->currentText().toStdString(), lvl))
        imsg::set_log_level(lvl);

    {
        std::lock_guard<std::mutex> lk(*logMutex_);
        logBuffer_->clear();
    }
    logView_->clear();
    openBtn_->setEnabled(false);
    lastOutputDir_ = QString::fromStdString(out_dir);

    // Persist a "job" record so it can be resumed / recovered after close.
    saveSettings();
    QSettings s;
    s.setValue("job/state", "running");
    s.setValue("job/done", 0);
    s.setValue("job/total", 0);
    jobRunning_ = true;

    // Live log: append each line to the pane on the GUI thread, and buffer for
    // the on-disk log file.
    auto buf = logBuffer_;
    auto mtx = logMutex_;
    imsg::set_log_sink([this, buf, mtx](imsg::LogLevel level, const std::string& msg) {
        const QString line =
            QString("[") + imsg::log_level_name(level) + "] " + QString::fromStdString(msg);
        {
            std::lock_guard<std::mutex> lk(*mtx);
            buf->push_back(line.toStdString());
        }
        QMetaObject::invokeMethod(
            this, [this, line] { logView_->appendPlainText(line); },
            Qt::QueuedConnection);
    });

    // Progress: marshal to the GUI thread.
    opts.on_progress = [this](int done, int total) {
        QMetaObject::invokeMethod(
            this, [this, done, total] { onProgress(done, total); },
            Qt::QueuedConnection);
    };

    // Pause/Stop: the engine calls this between conversations on the worker
    // thread. While paused we block here (so finished files are kept and the run
    // resumes cleanly); returning true asks the engine to stop early.
    stopRequested_.store(false);
    paused_.store(false);
    opts.should_stop = [this]() -> bool {
        while (paused_.load() && !stopRequested_.load()) QThread::msleep(150);
        return stopRequested_.load();
    };

    // Rich link previews: install a fetcher the HTML renderer calls per link.
    // It runs on the export worker thread (synchronous network), so set it
    // before launching and clear it in exportFinished(). Only HTML/PDF render
    // link cards; harmless otherwise.
    if (richPreviews_->isChecked())
        imsg::set_link_preview_resolver(
            [](const std::string& url) { return linkpreview::fetch_og_card(url); });
    else
        imsg::set_link_preview_resolver(nullptr);

    setBusy(true);
    watcher_.setFuture(QtConcurrent::run([db_path, out_dir, fmt, opts]() {
        return imsg::export_database(db_path, out_dir, fmt, opts);
    }));
}

void MainWindow::onProgress(int done, int total) {
    const int pct = total > 0 ? static_cast<int>(static_cast<long long>(done) * 100 / total) : 0;
    status_->setText(total > 0
                         ? QString("Exporting… %1% (%2 of %3 conversations)")
                               .arg(pct).arg(done).arg(total)
                         : QString("Exporting… %1 conversations").arg(done));
    if (progress_) {
        progress_->setRange(0, total > 0 ? total : 0);  // 0,0 stays indeterminate
        progress_->setValue(done);
    }
    QSettings s;
    s.setValue("job/done", done);
    s.setValue("job/total", total);
}

void MainWindow::writeLogFile(const QStringList& lines) {
    if (logFilePath_.isEmpty()) return;
    QFile f(logFilePath_);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream out(&f);
    out << "=== " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ===\n";
    for (const QString& line : lines) out << line << "\n";
    out << "\n";
}

void MainWindow::showExportError(const QString& error, const QString& title) {
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("The export could not be completed:", &dlg));

    auto* text = new QPlainTextEdit(error, &dlg);
    text->setReadOnly(true);
    text->setMinimumSize(460, 130);
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(&dlg);
    QPushButton* copyBtn = buttons->addButton("Copy error", QDialogButtonBox::ActionRole);
    QPushButton* logBtn = buttons->addButton("Open log file", QDialogButtonBox::ActionRole);
    QPushButton* settingsBtn = error.contains("Full Disk Access")
        ? buttons->addButton("Full Disk Access help…", QDialogButtonBox::ActionRole)
        : nullptr;
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(copyBtn, &QPushButton::clicked, this,
            [error] { QApplication::clipboard()->setText(error); });
    connect(logBtn, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(logFilePath_)); });
    if (settingsBtn)
        connect(settingsBtn, &QPushButton::clicked, &dlg,
                [this] { showFullDiskAccessHelp(); });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

void MainWindow::exportFinished() {
    imsg::set_log_sink(nullptr);
    imsg::set_link_preview_resolver(nullptr);
    jobRunning_ = false;
    QSettings().setValue("job/state", "finished");
    QStringList lines;
    {
        std::lock_guard<std::mutex> lk(*logMutex_);
        for (const std::string& line : *logBuffer_)
            lines << QString::fromStdString(line);
        logBuffer_->clear();
    }
    for (const QString& line : lines) logView_->appendPlainText(line);

    const imsg::ExportSummary s = watcher_.result();
    if (!s.ok) lines << ("ERROR: " + QString::fromStdString(s.error));
    writeLogFile(lines);

    setBusy(false);
    if (!s.ok) {
        status_->setText("Failed.");
        showExportError(QString::fromStdString(s.error));
        return;
    }
    if (s.conversations == 0) {
        status_->setText("No conversations matched.");
        QMessageBox::information(this, "Nothing exported",
                                 "No conversations with messages were found "
                                 "(check the date range or source).");
        return;
    }

    // PDF: convert the intermediate HTML files into PDFs in the chosen folder.
    if (wantPdf_) {
        QDir().mkpath(pdfRealOut_);
        int made = 0;
        const QFileInfoList htmls =
            QDir(pdfHtmlDir_).entryInfoList({"*.html"}, QDir::Files);
        for (const QFileInfo& fi : htmls) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            QTextDocument doc;
            // Base URL so relative copied-attachment paths (e.g. "Chat/photo.jpg")
            // resolve to the files alongside the intermediate HTML, letting the
            // pictures embed into the PDF.
            doc.setBaseUrl(QUrl::fromLocalFile(QDir(pdfHtmlDir_).absolutePath() + "/"));
            doc.setHtml(QString::fromUtf8(f.readAll()));
            f.close();
            QPdfWriter writer(QDir(pdfRealOut_).filePath(fi.completeBaseName() + ".pdf"));
            writer.setPageSize(QPageSize(QPageSize::A4));
            doc.print(&writer);
            ++made;
        }
        lastOutputDir_ = pdfRealOut_;
        status_->setText(QString("Exported %1 PDF(s).").arg(made));
        openBtn_->setEnabled(true);
        QMessageBox::information(
            this, "Export complete",
            QString("Export complete — %1 PDF(s) saved to:\n%2").arg(made).arg(pdfRealOut_));
        maybeUploadToDrive(pdfRealOut_);
        return;
    }

    QString msg = QString("Exported %1 conversation(s)").arg(s.conversations);
    if (s.attachments_copied > 0)
        msg += QString(", %1 attachment(s)").arg(s.attachments_copied);
    msg += ".";
    status_->setText(msg);
    openBtn_->setEnabled(true);
    QMessageBox::information(this, "Export complete",
                            msg + "\n\nSaved to:\n" + lastOutputDir_);
    maybeUploadToDrive(lastOutputDir_);
}

void MainWindow::openOutputDir() {
    if (!lastOutputDir_.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(lastOutputDir_));
}

void MainWindow::saveSettings() const {
    QSettings s;
    s.setValue("ui/source", source_->currentIndex());
    s.setValue("ui/db", dbPath_->text());
    s.setValue("ui/format", format_->currentText());
    s.setValue("ui/theme", themeCombo_->currentText());
    s.setValue("ui/output", outputDir_->text());
    s.setValue("ui/me", meLabel_->text());
    // New keys (the old ui/since/ui/until used a 1-year-ago default that would
    // now read as an active filter; ignoring them resets everyone to "(any)").
    s.setValue("ui/sinceDate", since_->date().toString("yyyy-MM-dd"));
    s.setValue("ui/untilDate", until_->date().toString("yyyy-MM-dd"));
    s.setValue("ui/combined", combined_->isChecked());
    s.setValue("ui/statsCover", statsCover_->isChecked());
    s.setValue("ui/copy", copyAttachments_->isChecked());
    s.setValue("ui/embed", embedAttachments_->isChecked());
    s.setValue("ui/hiddenAttach", hiddenAttachDir_->isChecked());
    s.setValue("ui/richPreviews", richPreviews_->isChecked());
    s.setValue("ui/uploadDrive", uploadDrive_->isChecked());
    s.setValue("ui/driveFolder", driveFolder_->text());
    s.setValue("ui/contacts", contacts_->currentIndex());
    s.setValue("ui/contactsPath", contactsPath_->text());
    s.setValue("ui/logLevel", logLevel_->currentText());
    s.setValue("ui/people", selectedPeople_);
}

void MainWindow::loadSettings() {
    QSettings s;
    if (!s.contains("ui/format")) return;  // first run: keep defaults
    source_->setCurrentIndex(s.value("ui/source", source_->currentIndex()).toInt());
    dbPath_->setText(s.value("ui/db").toString());
    format_->setCurrentText(s.value("ui/format", "html").toString());
    themeCombo_->setCurrentText(s.value("ui/theme", "ios").toString());
    outputDir_->setText(s.value("ui/output", outputDir_->text()).toString());
    meLabel_->setText(s.value("ui/me", "Me").toString());
    const QDate sd = QDate::fromString(s.value("ui/sinceDate").toString(), "yyyy-MM-dd");
    if (sd.isValid()) since_->setDate(sd);
    const QDate ud = QDate::fromString(s.value("ui/untilDate").toString(), "yyyy-MM-dd");
    if (ud.isValid()) until_->setDate(ud);
    updateDateSummary();
    combined_->setChecked(s.value("ui/combined", false).toBool());
    statsCover_->setChecked(s.value("ui/statsCover", false).toBool());
    copyAttachments_->setChecked(s.value("ui/copy", true).toBool());
    embedAttachments_->setChecked(s.value("ui/embed", false).toBool());
    hiddenAttachDir_->setChecked(s.value("ui/hiddenAttach", false).toBool());
    richPreviews_->setChecked(s.value("ui/richPreviews", false).toBool());
    uploadDrive_->setChecked(s.value("ui/uploadDrive", false).toBool());
    driveFolder_->setText(s.value("ui/driveFolder").toString());
    contacts_->setCurrentIndex(s.value("ui/contacts", 0).toInt());
    contactsPath_->setText(s.value("ui/contactsPath").toString());
    logLevel_->setCurrentText(s.value("ui/logLevel", "info").toString());
    selectedPeople_ = s.value("ui/people").toStringList();
    peopleLabel_->setText(selectedPeople_.isEmpty()
                              ? "All conversations"
                              : QString("%1 selected").arg(selectedPeople_.size()));
    onSourceChanged();
    onContactsChanged();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    const QString msg = jobRunning_
                            ? "An export is still running. Close anyway? You can "
                              "resume it next time."
                            : "Close iMessage Exporter?";
    if (QMessageBox::question(this, "Quit", msg) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    saveSettings();
    if (jobRunning_) QSettings().setValue("job/state", "interrupted");
    event->accept();
}

void MainWindow::maybeResumePrevious() {
    QSettings s;
    const QString state = s.value("job/state").toString();
    if (state != "running" && state != "interrupted") return;

    const bool crashed = (state == "running");  // never marked interrupted => crash
    const int done = s.value("job/done", 0).toInt();
    const int total = s.value("job/total", 0).toInt();
    const QString card =
        QString("<b>Previous export</b><br>Source: %1<br>Format: %2<br>"
                "Output: %3<br>Progress: %4 of %5 conversations%6")
            .arg(source_->currentText(), format_->currentText().toUpper(),
                 outputDir_->text())
            .arg(done)
            .arg(total)
            .arg(selectedPeople_.isEmpty()
                     ? ""
                     : QString("<br>People: %1 selected").arg(selectedPeople_.size()));

    QMessageBox box(this);
    box.setWindowTitle(crashed ? "Recover unfinished export" : "Resume export");
    box.setTextFormat(Qt::RichText);
    box.setText((crashed ? "The last export did not shut down cleanly.<br><br>"
                         : "An export was interrupted when you last closed.<br><br>") +
                card);
    QPushButton* resume = box.addButton(crashed ? "Recover" : "Resume",
                                        QMessageBox::AcceptRole);
    box.addButton("Start new", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == resume)
        startExportResuming(true);
    else
        s.remove("job");  // discard the old job
}

void MainWindow::pickPeople() {
    std::string db_path, out_dir;
    imsg::Format fmt = imsg::Format::Text;
    imsg::ExportOptions opts;
    QString error;
    if (!buildInputs(db_path, out_dir, fmt, opts, error)) {
        QMessageBox::warning(this, "Select people", error);
        return;
    }

    imsg::ContactBook book;
    if (!opts.contacts_path.empty())
        book = imsg::load_contacts(opts.contacts_path);
    else if (opts.use_contacts)
        book = imsg::load_contacts_default();
    if (opts.use_contact_store) {
        imsg::ContactStore st(imsg::default_contact_store_path());
        if (st.open()) st.load_into(book);
    }

    // One row per distinct participant; carries everything the dialog needs to
    // sort/filter/display, plus the canonical "Name  —  +handle" we must store
    // (the export filter matches that substring-both-ways).
    struct PersonRow {
        QString canonical;  // stored into selectedPeople_; matched by the filter
        QString display;    // richer label shown in the list
        QString name;       // resolved name, or "" when only a handle is known
        QString lastName;   // last whitespace token of name (for "Last name" sort)
        QString service;    // iMessage / SMS / RCS, or ""
        std::time_t lastDate = 0;
        bool hasLast = false;
    };
    std::vector<PersonRow> rows;
    QHash<QString, QString> photoByCanon;  // canonical -> photo data URI/URL
    try {
        // Keep raw handles (no set_contacts) so we can show "Name — number".
        imsg::MessagesDatabase db(db_path, opts.me_label);
        db.open();
        QHash<QString, imsg::HandleStat> statByHandle;  // raw handle -> stats
        for (const imsg::HandleStat& s : db.handle_stats())
            statByHandle.insert(QString::fromStdString(s.handle), s);

        QSet<QString> seen;
        for (const imsg::Chat& c : db.load_chat_index()) {
            if (c.message_count == 0) continue;
            for (const std::string& p : c.participants) {
                if (p.empty()) continue;
                const QString handle = QString::fromStdString(p);
                const std::string nm = book.name_for(p);  // "" when unresolved
                PersonRow r;
                r.name = QString::fromStdString(nm);
                r.canonical = r.name.isEmpty() ? handle : r.name + "  —  " + handle;
                if (seen.contains(r.canonical)) continue;
                seen.insert(r.canonical);

                const imsg::HandleStat st = statByHandle.value(handle);
                r.service = QString::fromStdString(st.service);
                r.lastDate = st.last_date;
                r.hasLast = st.has_last;
                const QString lastStr =
                    r.hasLast ? QDateTime::fromSecsSinceEpoch(r.lastDate).date().toString(
                                    "yyyy-MM-dd")
                              : QStringLiteral("—");
                // Last whitespace-separated token of the name (for "Last name"
                // sort); falls back to the whole canonical when there's no name.
                const QStringList parts =
                    r.name.simplified().split(' ', Qt::SkipEmptyParts);
                r.lastName = parts.isEmpty() ? r.canonical : parts.last();
                r.display = r.canonical + "   ·  " +
                            (r.service.isEmpty() ? QStringLiteral("—") : r.service) +
                            "  ·  " + lastStr;
                photoByCanon.insert(r.canonical, QString::fromStdString(book.photo_for(p)));
                rows.push_back(std::move(r));
            }
        }
    } catch (const imsg::DatabaseError& e) {
        showExportError(QString::fromUtf8(e.what()), "Cannot read messages");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Select people to export");
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Export only conversations with the checked people "
                                 "(none checked = all):",
                                 &dlg));

    // Controls row: sort order + free-text filter.
    auto* ctrlRow = new QHBoxLayout;
    auto* sortCombo = new QComboBox(&dlg);
    sortCombo->addItems({"Name (A–Z)", "Last name", "Last text (newest)",
                         "Last text (oldest)", "Service"});
    auto* filterEdit = new QLineEdit(&dlg);
    filterEdit->setPlaceholderText("Filter…");
    filterEdit->setClearButtonEnabled(true);
    ctrlRow->addWidget(new QLabel("Sort by:", &dlg));
    ctrlRow->addWidget(sortCombo);
    ctrlRow->addWidget(filterEdit, 1);
    layout->addLayout(ctrlRow);

    auto* list = new QListWidget(&dlg);
    list->setIconSize(QSize(28, 28));
    list->setMinimumSize(460, 320);
    layout->addWidget(list);

    // Checked state survives re-sorting/-filtering (a hidden-by-filter item stays
    // checked), so track it by canonical rather than reading the live widgets.
    QSet<QString> checked;
    for (const QString& s : selectedPeople_) checked.insert(s);
    auto syncCheckedFromList = [list, &checked] {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* it = list->item(i);
            const QString canon = it->data(Qt::UserRole).toString();
            if (it->checkState() == Qt::Checked)
                checked.insert(canon);
            else
                checked.remove(canon);
        }
    };

    // Clears and repopulates the list from `rows`, applying the chosen sort and
    // the case-insensitive substring filter (matched against the shown text).
    auto repopulate = [&] {
        std::vector<const PersonRow*> view;
        view.reserve(rows.size());
        const QString needle = filterEdit->text().trimmed();
        for (const PersonRow& r : rows)
            if (needle.isEmpty() || r.display.contains(needle, Qt::CaseInsensitive))
                view.push_back(&r);

        const int mode = sortCombo->currentIndex();
        std::stable_sort(view.begin(), view.end(),
                         [mode](const PersonRow* a, const PersonRow* b) {
            switch (mode) {
                case 1:  // Last name (then full name to break ties)
                    if (int c = a->lastName.compare(b->lastName, Qt::CaseInsensitive))
                        return c < 0;
                    return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
                case 2:  // Last text, newest first (dated before undated)
                    if (a->hasLast != b->hasLast) return a->hasLast;
                    if (a->lastDate != b->lastDate) return a->lastDate > b->lastDate;
                    return a->canonical.compare(b->canonical, Qt::CaseInsensitive) < 0;
                case 3:  // Last text, oldest first (dated before undated)
                    if (a->hasLast != b->hasLast) return a->hasLast;
                    if (a->lastDate != b->lastDate) return a->lastDate < b->lastDate;
                    return a->canonical.compare(b->canonical, Qt::CaseInsensitive) < 0;
                case 4:  // Service, then name
                    if (int c = a->service.compare(b->service, Qt::CaseInsensitive))
                        return c < 0;
                    return a->canonical.compare(b->canonical, Qt::CaseInsensitive) < 0;
                default:  // Name (A–Z)
                    return a->canonical.compare(b->canonical, Qt::CaseInsensitive) < 0;
            }
        });

        list->clear();
        for (const PersonRow* r : view) {
            auto* item = new QListWidgetItem(r->display, list);
            item->setData(Qt::UserRole, r->canonical);  // value the filter matches
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(checked.contains(r->canonical) ? Qt::Checked
                                                               : Qt::Unchecked);
            const QIcon ic = iconFromDataUri(photoByCanon.value(r->canonical));
            if (!ic.isNull()) item->setIcon(ic);
        }
    };

    // Re-sort/-filter live; keep prior checks by syncing them out first.
    connect(sortCombo, &QComboBox::currentIndexChanged, &dlg,
            [&] { syncCheckedFromList(); repopulate(); });
    connect(filterEdit, &QLineEdit::textChanged, &dlg,
            [&] { syncCheckedFromList(); repopulate(); });
    repopulate();

    auto* selRow = new QHBoxLayout;
    auto* selAllBtn = new QPushButton("Select all", &dlg);
    auto* selNoneBtn = new QPushButton("Unselect all", &dlg);
    // Operate on the currently visible (filtered) items.
    connect(selAllBtn, &QPushButton::clicked, &dlg, [list] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Checked);
    });
    connect(selNoneBtn, &QPushButton::clicked, &dlg, [list] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Unchecked);
    });
    selRow->addWidget(selAllBtn);
    selRow->addWidget(selNoneBtn);
    selRow->addStretch();
    layout->addLayout(selRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    syncCheckedFromList();  // fold in any items hidden by an active filter
    selectedPeople_.clear();
    for (const QString& canon : checked) selectedPeople_ << canon;
    selectedPeople_.sort(Qt::CaseInsensitive);
    peopleLabel_->setText(selectedPeople_.isEmpty()
                              ? "All conversations"
                              : QString("%1 selected").arg(selectedPeople_.size()));
}

void MainWindow::copyMessagesData() {
    const QString src =
        (source_->currentData().toInt() == SrcFile && !dbPath_->text().isEmpty())
            ? dbPath_->text()
            : QString::fromStdString(imsg::default_db_path());
    if (!QFile::exists(src)) {
        QMessageBox::warning(this, "Copy Messages data",
                             "No Messages database found at:\n" + src);
        return;
    }
    const QString destDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath("messages");
    QDir().mkpath(destDir);
    const QString dest = QDir(destDir).filePath("chat.db");
    QFile::remove(dest);
    if (!QFile::copy(src, dest)) {
        showExportError(
            "Could not read " + src +
                ".\nOn macOS the live Messages database is protected, so even "
                "copying it needs Full Disk Access for this app (grant it, then "
                "try again). After one successful copy, future runs read the cache "
                "without Full Disk Access.",
            "Copy Messages data");
        return;
    }
    for (const QString& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        QFile::remove(dest + suffix);
        QFile::copy(src + suffix, dest + suffix);  // best-effort
    }
    const int fileIdx = source_->findData(SrcFile);
    if (fileIdx >= 0) source_->setCurrentIndex(fileIdx);
    dbPath_->setText(dest);
    onSourceChanged();
    status_->setText("Copied the Messages database to a local cache; now reading "
                     "from it (no Full Disk Access needed next time).");
}

void MainWindow::importICloudContacts() {
    QDialog dlg(this);
    dlg.setWindowTitle("Import iCloud Contacts");
    auto* form = new QFormLayout(&dlg);

    auto* info = new QLabel(
        "Connect to iCloud over CardDAV to import your contacts for name "
        "resolution.<br><br>"
        "Use an <b>app-specific password</b>, not your normal Apple ID password: "
        "create one at "
        "<a href=\"https://account.apple.com\">account.apple.com</a> &rarr; "
        "Sign-In and Security &rarr; App-Specific Passwords.",
        &dlg);
    info->setTextFormat(Qt::RichText);
    info->setOpenExternalLinks(true);
    info->setWordWrap(true);
    form->addRow(info);

    auto* idField = new QLineEdit(secret::retrieve("icloud_apple_id"), &dlg);
    idField->setPlaceholderText("you@icloud.com");
    auto* pwField = new QLineEdit(secret::retrieve("icloud_app_password"), &dlg);
    pwField->setEchoMode(QLineEdit::Password);
    pwField->setPlaceholderText("xxxx-xxxx-xxxx-xxxx");
    form->addRow("Apple ID:", idField);
    form->addRow("App password:", pwField);

    auto* saveCreds = new QCheckBox("Save credentials (encrypted in the OS keychain)", &dlg);
    saveCreds->setChecked(!idField->text().isEmpty());
    form->addRow("", saveCreds);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;
    const QString appleId = idField->text().trimmed();
    const QString appPw = pwField->text().trimmed();
    if (appleId.isEmpty() || appPw.isEmpty()) return;

    // Persist (encrypted) only when asked; otherwise make sure nothing lingers.
    if (saveCreds->isChecked()) {
        secret::store("icloud_apple_id", appleId);
        secret::store("icloud_app_password", appPw);
    } else {
        secret::remove("icloud_apple_id");
        secret::remove("icloud_app_password");
    }

    icloudBtn_->setEnabled(false);
    status_->setText("Connecting to iCloud…");
    icloudWatcher_.setFuture(QtConcurrent::run(
        [appleId, appPw]() { return icloud::fetchContacts(appleId, appPw); }));
}

void MainWindow::icloudFinished() {
    icloudBtn_->setEnabled(true);
    const icloud::Result result = icloudWatcher_.result();
    if (!result.ok) {
        status_->setText("iCloud import failed.");
        QMessageBox::warning(
            this, "iCloud import failed",
            result.error +
                "\n\nAlternatively, export a vCard at iCloud.com → Contacts "
                "and choose \"Contacts file…\".");
        return;
    }
    // Merge iCloud contacts into the shared store (alongside Google), with
    // photos, so both address books are used together rather than either/or.
    imsg::ContactStore store(imsg::default_contact_store_path());
    int n = 0;
    if (store.open()) {
        for (const imsg::VCardEntry& e :
             imsg::parse_vcard_entries(result.vcards.toStdString())) {
            store.upsert(e.handle, e.name, "icloud", e.photo);
            ++n;
        }
    } else {
        QMessageBox::warning(this, "iCloud import",
                             "Could not open the saved contacts database.");
        return;
    }
    contacts_->setCurrentIndex(CtStore);  // the merged "Saved contacts database"
    onContactsChanged();
    status_->setText(QString("Imported %1 iCloud contact entr%2 into the saved "
                             "database (merged with Google).")
                         .arg(n)
                         .arg(n == 1 ? "y" : "ies"));
}

void MainWindow::showHowToGetData() {
    richTextDialog(
        this, "How to get your data",
        "<h3>Messages (chat.db)</h3>"
        "<ul>"
        "<li><b>From a Mac:</b> with Messages signed in (turn on <i>Messages in "
        "iCloud</i> for your full history), the database is at "
        "<code>~/Library/Messages/chat.db</code>. The app/terminal needs "
        "<a href=\"https://support.apple.com/guide/mac-help/mchld5a35146/mac\">"
        "Full Disk Access</a>.</li>"
        "<li><b>From an iPhone backup:</b> make an <b>unencrypted</b> local backup "
        "in Finder/iTunes, then pick the \"Device backup\" source.</li>"
        "<li>iCloud has <b>no web or API access to Messages</b> (they are "
        "end-to-end encrypted), so message history must come from a Mac or a "
        "device backup.</li>"
        "</ul>"
        "<h3>Contacts (for names instead of phone numbers)</h3>"
        "<ul>"
        "<li><b>iCloud, in this app:</b> use \"Import iCloud Contacts…\" with an "
        "<a href=\"https://account.apple.com\">app-specific password</a>.</li>"
        "<li><b>vCard export:</b> at "
        "<a href=\"https://www.icloud.com/contacts\">iCloud.com &rarr; Contacts</a>, "
        "select all and Export vCard, then choose \"Contacts file…\".</li>"
        "<li><b>On a Mac:</b> the local Contacts database is detected "
        "automatically.</li>"
        "</ul>"
        "<p>Full guide in the "
        "<a href=\"https://github.com/grioghar/imessage-exporter-redux#readme\">"
        "online documentation</a>.</p>");
}

void MainWindow::showGoogleSetup() {
    richTextDialog(
        this, "Google Contacts setup",
        "<h3>Create a Google OAuth client (one-time)</h3>"
        "<p>Google moved this into the <b>Google Auth Platform</b> (the old "
        "\"OAuth consent screen\" page now redirects there).</p>"
        "<p>Each step links straight to the page you need:</p>"
        "<ol>"
        "<li>Open the <a href=\"https://console.cloud.google.com/projectcreate\">"
        "New Project</a> page (or pick an existing project).</li>"
        "<li>Enable the "
        "<a href=\"https://console.cloud.google.com/apis/library/people.googleapis.com\">"
        "People API</a> (and, for Drive upload, the "
        "<a href=\"https://console.cloud.google.com/apis/library/drive.googleapis.com\">"
        "Google Drive API</a>) → <b>Enable</b>.</li>"
        "<li><a href=\"https://console.cloud.google.com/auth/overview\">Google Auth "
        "Platform</a> → <b>Get started</b>:"
        "<ul>"
        "<li><b><a href=\"https://console.cloud.google.com/auth/branding\">Branding</a>:</b> "
        "app name + your support/contact email.</li>"
        "<li><b><a href=\"https://console.cloud.google.com/auth/audience\">Audience</a>:</b> "
        "User type <b>External</b>; keep <b>Testing</b>; under <b>Test users</b> add "
        "the Google account you'll export.</li>"
        "<li><b><a href=\"https://console.cloud.google.com/auth/scopes\">Data access</a>:</b> "
        "add <code>.../auth/contacts.readonly</code> (and, for Drive upload, "
        "<code>.../auth/drive.file</code>).</li>"
        "</ul></li>"
        "<li><a href=\"https://console.cloud.google.com/auth/clients\">Clients</a> → "
        "<b>Create client</b>, Application type <b>Desktop app</b>.</li>"
        "<li><b>Download JSON</b> for the client, then use <b>Import client JSON…</b> "
        "in the Connect dialog (or paste the <b>Client ID</b> + <b>Client secret</b>). "
        "Tick <b>Save credentials</b> to keep them encrypted in your OS keychain.</li>"
        "</ol>"
        "<p><b>Note:</b> <code>contacts.readonly</code> is a sensitive scope, so the "
        "app stays in <b>Testing</b> (no Google verification needed). Google then "
        "expires the token after <b>7 days</b> — harmless here, since each "
        "\"Connect\" re-authorizes; just sign in again if it's been a week.</p>"
        "<p>Credentials and tokens are stored <b>encrypted in your OS keychain</b>. "
        "<b>Google Drive:</b> once the <code>drive.file</code> scope is added, click "
        "<b>Connect Google Drive…</b>; the authorization is saved so future exports "
        "can upload to your chosen folder automatically (the app can only touch the "
        "files it creates). Full guide: "
        "<a href=\"https://github.com/grioghar/imessage-exporter-redux/blob/main/"
        "docs/GOOGLE.md\">docs/GOOGLE.md</a>.</p>");
}

void MainWindow::showFullDiskAccessHelp() {
    QString appBundle = QApplication::applicationDirPath();  // …/X.app/Contents/MacOS
    const int dotApp = appBundle.indexOf(".app/");
    if (dotApp > 0) appBundle = appBundle.left(dotApp + 4);
    const bool translocated = appBundle.contains("/AppTranslocation/");

    QDialog dlg(this);
    dlg.setWindowTitle("Full Disk Access");
    auto* layout = new QVBoxLayout(&dlg);
    auto* label = new QLabel(
        "<h3>Let the app read Messages &amp; Contacts directly</h3>"
        "<p>macOS needs <b>Full Disk Access</b> for this app to read "
        "<code>~/Library/Messages/chat.db</code>. If the app won't appear when you "
        "press <b>+</b> in that list, it's almost always because the copy you're "
        "running is still <i>quarantined</i> or running <i>translocated</i> from the "
        "disk image.</p>"
        "<ol>"
        "<li><b>Move the app to Applications.</b> Drag <b>iMessage Exporter</b> from "
        "the .dmg into <b>/Applications</b> (or <b>~/Applications</b>) and launch it "
        "from there — not from the mounted disk image.</li>"
        "<li><b>Remove the quarantine flag</b> (this is what makes it show up in the "
        "list). Click <b>Copy de-quarantine command</b>, paste it into <b>Terminal</b>, "
        "and press Return.</li>"
        "<li><b>Add it to Full Disk Access.</b> Click <b>Open Full Disk Access "
        "settings</b>, press <b>+</b>, choose the app in /Applications, switch it "
        "<b>on</b>, then re-open iMessage Exporter.</li>"
        "</ol>"
        "<p><b>Prefer not to use Terminal?</b> Click <b>\"Copy Messages data to a local "
        "cache\"</b> on the main window — it copies <code>chat.db</code> somewhere "
        "readable so you can export without granting Full Disk Access at all.</p>",
        &dlg);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);
    label->setMinimumWidth(560);
    layout->addWidget(label);

    if (translocated) {
        auto* warn = new QLabel(
            "<p style='color:#b00020'>⚠︎ This copy is running <b>translocated</b> from a "
            "read-only location, so it can't be added to Full Disk Access. Move it to "
            "/Applications and relaunch first.</p>",
            &dlg);
        warn->setTextFormat(Qt::RichText);
        warn->setWordWrap(true);
        layout->addWidget(warn);
    }

    auto* buttons = new QDialogButtonBox(&dlg);
    QPushButton* openSettings =
        buttons->addButton("Open Full Disk Access settings", QDialogButtonBox::ActionRole);
    QPushButton* copyCmd =
        buttons->addButton("Copy de-quarantine command", QDialogButtonBox::ActionRole);
    QPushButton* reveal =
        buttons->addButton("Reveal app in Finder", QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(openSettings, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(
            "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"));
    });
    connect(copyCmd, &QPushButton::clicked, this, [appBundle] {
        QApplication::clipboard()->setText(
            "xattr -dr com.apple.quarantine \"" + appBundle + "\"");
    });
    connect(reveal, &QPushButton::clicked, this, [appBundle] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(appBundle).absolutePath()));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

bool MainWindow::promptForGoogleClient(QString& id, QString& secret) {
    QDialog dlg(this);
    dlg.setWindowTitle("Google OAuth client");
    auto* form = new QFormLayout(&dlg);
    auto* info = new QLabel(
        "This needs a Google Cloud OAuth <b>Desktop app</b> client. Enter the "
        "Client ID/secret, or <b>Import client JSON</b> (the file Google Cloud "
        "Console gives you) — see <b>Help → Google Contacts setup…</b>.",
        &dlg);
    info->setTextFormat(Qt::RichText);
    info->setWordWrap(true);
    form->addRow(info);

    auto* idField = new QLineEdit(googleauth::clientId(), &dlg);
    idField->setPlaceholderText("xxxxxxxx.apps.googleusercontent.com");
    auto* secretField = new QLineEdit(googleauth::clientSecret(), &dlg);
    secretField->setEchoMode(QLineEdit::Password);
    secretField->setPlaceholderText("client secret");
    form->addRow("Client ID:", idField);
    form->addRow("Client secret:", secretField);

    // Load the credentials straight from the OAuth client JSON Google hands out.
    auto* importBtn = new QPushButton("Import client JSON…", &dlg);
    connect(importBtn, &QPushButton::clicked, &dlg, [this, &dlg, idField, secretField] {
        const QString path = QFileDialog::getOpenFileName(
            &dlg, "Select Google OAuth client JSON", QString(), "JSON (*.json)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Import client JSON", "Could not read that file.");
            return;
        }
        QString jid, jsec, err;
        if (!googleauth::parseClientJson(f.readAll(), jid, jsec, err)) {
            QMessageBox::warning(this, "Import client JSON", err);
            return;
        }
        idField->setText(jid);
        secretField->setText(jsec);
    });
    form->addRow("", importBtn);

    auto* saveCreds = new QCheckBox("Save credentials (encrypted in the OS keychain)", &dlg);
    saveCreds->setChecked(true);
    form->addRow("", saveCreds);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText("Continue");
    QPushButton* setupBtn = buttons->addButton("Setup help", QDialogButtonBox::HelpRole);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(setupBtn, &QPushButton::clicked, this, &MainWindow::showGoogleSetup);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;
    id = idField->text().trimmed();
    secret = secretField->text().trimmed();
    if (id.isEmpty()) {
        QMessageBox::warning(this, "Google", "A Client ID is required (see Setup help).");
        return false;
    }
    // Persist (encrypted) only if asked; either way the values are returned for
    // this run.
    if (saveCreds->isChecked())
        googleauth::storeClient(id, secret);
    else
        googleauth::clearClient();
    return true;
}

void MainWindow::connectGoogle() {
    QString id, sec;
    if (!promptForGoogleClient(id, sec)) return;
    google_->setClient(id, sec);
    status_->setText("Connecting to Google…");
    google_->connectAndDownload();
}

void MainWindow::connectDrive() {
    if (!googleauth::configured()) {  // need an OAuth client first
        QString id, sec;
        if (!promptForGoogleClient(id, sec)) return;
        drive_->setClient(id, sec);
    }
    status_->setText("Connecting to Google Drive…");
    drive_->connectInteractive();
}

void MainWindow::maybeUploadToDrive(const QString& dir) {
    if (!uploadDrive_->isChecked()) return;
    if (!GoogleDrive::isConnected()) {
        QMessageBox::information(
            this, "Google Drive",
            "The export is done, but Google Drive isn't connected, so nothing was "
            "uploaded. Click \"Connect Google Drive…\" and export again to upload.");
        return;
    }
    const QString folder = driveFolder_->text();
    status_->setText("Uploading the export to Google Drive…");
    driveWatcher_.setFuture(QtConcurrent::run(
        [dir, folder] { return drive::uploadDirectory(dir, folder); }));
}

void MainWindow::driveUploadFinished() {
    const drive::UploadResult r = driveWatcher_.result();
    if (r.ok) {
        status_->setText(QString("Uploaded %1 file(s) to Google Drive.").arg(r.files));
    } else {
        status_->setText("Google Drive upload failed.");
        QMessageBox::warning(this, "Google Drive upload", r.error);
    }
}

void MainWindow::runUpdateCheck(bool manual) {
    manualUpdateCheck_ = manual;
    if (manual) status_->setText("Checking for updates…");
    updater_->checkForUpdates();
}

void MainWindow::showAbout() {
    richTextDialog(
        this, "About iMessage Exporter",
        "<h3>iMessage Exporter " IMSG_VERSION "." IMSG_BUILD_STAMP "</h3>"
        "<p>Export macOS iMessage / SMS history to TXT, JSON, or HTML.</p>"
        "<p>A small, fast C++ tool with a Qt desktop front-end, sharing one "
        "export engine across the CLI, desktop, and iOS.</p>"
        "<p><a href=\"https://github.com/grioghar/imessage-exporter-redux\">"
        "github.com/grioghar/imessage-exporter-redux</a><br>MIT License.</p>");
}
