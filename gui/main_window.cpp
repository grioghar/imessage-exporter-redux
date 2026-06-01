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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <ctime>
#include <string>

#include "imsg/backup.hpp"
#include "imsg/contact_store.hpp"
#include "imsg/contacts.hpp"
#include "imsg/database.hpp"
#include "imsg/exporters.hpp"
#include "imsg/log.hpp"
#include "imsg/build_stamp.hpp"
#include "imsg/time_util.hpp"
#include "imsg/version.hpp"

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
}  // namespace

MainWindow::MainWindow()
    : logMutex_(std::make_shared<std::mutex>()),
      logBuffer_(std::make_shared<std::vector<std::string>>()) {
    setWindowTitle("iMessage Exporter");

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    logFilePath_ = QDir(dataDir).filePath("imessage-exporter.log");

    auto* form = new QFormLayout;

    // --- Source -------------------------------------------------------------
    source_ = new QComboBox;
#if defined(Q_OS_MACOS)
    // Auto-detecting ~/Library/Messages/chat.db only makes sense on a Mac.
    source_->addItem("Auto-detect Messages on this Mac", SrcAuto);
#endif
    source_->addItem("Database file (chat.db / sms.db)…", SrcFile);
    source_->addItem("Device backup…", SrcBackup);
    form->addRow("Source:", source_);

    auto* dbRow = new QHBoxLayout;
    dbPath_ = new QLineEdit;
    dbPath_->setPlaceholderText("Path to a Messages database");
    dbBrowse_ = new QPushButton("Browse…");
    dbRow->addWidget(dbPath_);
    dbRow->addWidget(dbBrowse_);
    form->addRow("Database:", dbRow);

    backup_ = new QComboBox;
    for (const imsg::BackupInfo& b : imsg::list_backups()) {
        QString label = QString::fromStdString(b.udid) +
                        (b.encrypted ? "  [encrypted]" : "");
        backup_->addItem(label, QString::fromStdString(b.path));
    }
    if (backup_->count() == 0) backup_->addItem("(no backups found)", QString());
    form->addRow("Backup:", backup_);

    copyDbBtn_ = new QPushButton("Copy Messages data to a local cache…");
    copyDbBtn_->setToolTip("Copy chat.db to a folder this app can always read, so "
                           "you don't need Full Disk Access on every run. (The copy "
                           "itself needs read access once.)");
    form->addRow("", copyDbBtn_);

    // --- Options ------------------------------------------------------------
    format_ = new QComboBox;
    format_->addItems({"txt", "md", "html", "json", "pdf"});
    format_->setCurrentText("html");
    form->addRow("Format:", format_);

    auto* outRow = new QHBoxLayout;
    outputDir_ = new QLineEdit;
    outputDir_->setText(QDir::homePath() + "/iMessage Export");
    auto* outBrowse = new QPushButton("Browse…");
    outRow->addWidget(outputDir_);
    outRow->addWidget(outBrowse);
    form->addRow("Output folder:", outRow);

    meLabel_ = new QLineEdit("Me");
    form->addRow("Your name:", meLabel_);

    auto* sinceRow = new QHBoxLayout;
    sinceOn_ = new QCheckBox("From");
    since_ = new QDateEdit(QDate::currentDate().addYears(-1));
    since_->setDisplayFormat("yyyy-MM-dd");
    since_->setCalendarPopup(true);
    since_->setEnabled(false);
    untilOn_ = new QCheckBox("To");
    untilOn_->setToolTip("Leave unchecked to export from the 'From' date to now.");
    until_ = new QDateEdit(QDate::currentDate());
    until_->setDisplayFormat("yyyy-MM-dd");
    until_->setCalendarPopup(true);
    until_->setEnabled(false);
    sinceRow->addWidget(sinceOn_);
    sinceRow->addWidget(since_);
    sinceRow->addWidget(untilOn_);
    sinceRow->addWidget(until_);
    sinceRow->addStretch();
    form->addRow("Date range:", sinceRow);

    combined_ = new QCheckBox("Single combined file");
    copyAttachments_ = new QCheckBox("Copy attachment files");
    embedAttachments_ = new QCheckBox("Embed attachments (larger files)");
    embedAttachments_->setToolTip("Inline pictures, movies and files as base64 so "
                                  "each HTML/JSON file is self-contained.");
    auto* flags = new QHBoxLayout;
    flags->addWidget(combined_);
    flags->addWidget(copyAttachments_);
    flags->addWidget(embedAttachments_);
    flags->addStretch();
    form->addRow("", flags);

    contacts_ = new QComboBox;
    contacts_->addItem("None (show phone numbers / emails)");
    contacts_->addItem("This Mac's Contacts");
    contacts_->addItem("Contacts file (.abcddb / .vcf)…");
    contacts_->addItem("From the selected backup");
    contacts_->addItem("Saved contacts database (Google / imported)");
    form->addRow("Names:", contacts_);

    auto* ctRow = new QHBoxLayout;
    contactsPath_ = new QLineEdit;
    contactsPath_->setPlaceholderText("Path to .abcddb or .vcf");
    contactsBrowse_ = new QPushButton("Browse…");
    ctRow->addWidget(contactsPath_);
    ctRow->addWidget(contactsBrowse_);
    form->addRow("Contacts file:", ctRow);

    icloudBtn_ = new QPushButton("Import iCloud Contacts…");
    icloudBtn_->setToolTip("Fetch your iCloud contacts over CardDAV using an "
                           "app-specific password.");
    form->addRow("", icloudBtn_);

    googleBtn_ = new QPushButton("Connect Google Contacts…");
    googleBtn_->setToolTip("Sign in to Google and download your contacts into the "
                           "saved contacts database (needs IMSG_GOOGLE_CLIENT_ID).");
    form->addRow("", googleBtn_);

    auto* peopleRow = new QHBoxLayout;
    peopleBtn_ = new QPushButton("Select people…");
    peopleBtn_->setToolTip("Export only conversations with specific people.");
    peopleLabel_ = new QLabel("All conversations");
    peopleLabel_->setStyleSheet("color:#6e6e73");
    peopleRow->addWidget(peopleBtn_);
    peopleRow->addWidget(peopleLabel_);
    peopleRow->addStretch();
    form->addRow("People:", peopleRow);

    logLevel_ = new QComboBox;
    logLevel_->addItems({"error", "warn", "info", "debug"});
    logLevel_->setCurrentText("info");
    form->addRow("Log level:", logLevel_);

    // --- Actions / output ---------------------------------------------------
    exportBtn_ = new QPushButton("Export");
    openBtn_ = new QPushButton("Open output folder");
    openBtn_->setEnabled(false);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(exportBtn_);
    btnRow->addWidget(openBtn_);
    btnRow->addStretch();

    status_ = new QLabel("Ready.");
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMinimumHeight(160);

    // --- Menu bar (Help) ----------------------------------------------------
    auto* menuBar = new QMenuBar;
    QMenu* helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("How to get your data…", this, &MainWindow::showHowToGetData);
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
    root->addLayout(form);
    root->addLayout(btnRow);
    root->addWidget(status_);
    root->addWidget(logView_);

    connect(source_, &QComboBox::currentIndexChanged, this, &MainWindow::onSourceChanged);
    connect(contacts_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onContactsChanged);
    connect(dbBrowse_, &QPushButton::clicked, this, &MainWindow::browseDatabase);
    connect(outBrowse, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(contactsBrowse_, &QPushButton::clicked, this, &MainWindow::browseContacts);
    connect(sinceOn_, &QCheckBox::toggled, since_, &QWidget::setEnabled);
    connect(untilOn_, &QCheckBox::toggled, until_, &QWidget::setEnabled);
    connect(exportBtn_, &QPushButton::clicked, this, &MainWindow::startExport);
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
    connect(googleBtn_, &QPushButton::clicked, this, [this] {
        if (!GoogleContacts::configured()) {
            QMessageBox::information(
                this, "Google Contacts",
                "To enable this, create a Google Cloud OAuth \"Desktop app\" client "
                "for the People API and set IMSG_GOOGLE_CLIENT_ID (and "
                "IMSG_GOOGLE_CLIENT_SECRET) in your environment, then restart.");
            return;
        }
        google_->connectAndDownload();
    });

    onSourceChanged();
    onContactsChanged();
    loadSettings();
    QTimer::singleShot(0, this, [this] { maybeResumePrevious(); });
}

void MainWindow::onSourceChanged() {
    const int s = source_->currentData().toInt();
    const bool file = (s == SrcFile);
    const bool backup = (s == SrcBackup);
    dbPath_->setEnabled(file);
    dbBrowse_->setEnabled(file);
    backup_->setEnabled(backup);
}

void MainWindow::onContactsChanged() {
    const bool file = (contacts_->currentIndex() == CtFile);
    contactsPath_->setEnabled(file);
    contactsBrowse_->setEnabled(file);
}

void MainWindow::browseDatabase() {
    QString f = QFileDialog::getOpenFileName(this, "Choose a Messages database",
                                             QDir::homePath());
    if (!f.isEmpty()) dbPath_->setText(f);
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
    opts.combined = combined_->isChecked();
    opts.copy_attachments = copyAttachments_->isChecked();
    opts.embed_attachments = embedAttachments_->isChecked();

    if (sinceOn_->isChecked() &&
        imsg::parse_date(since_->date().toString("yyyy-MM-dd").toStdString(), opts.since))
        opts.has_since = true;
    if (untilOn_->isChecked() &&
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

    for (const QString& p : selectedPeople_)
        opts.only_participants.push_back(p.toStdString());
    return true;
}

void MainWindow::setBusy(bool busy) {
    exportBtn_->setEnabled(!busy);
    status_->setText(busy ? "Exporting…" : "Ready.");
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

    // PDF: write HTML to a temp dir now; convert to PDF in exportFinished().
    wantPdf_ = (format_->currentText() == "pdf");
    if (wantPdf_) {
        pdfRealOut_ = QString::fromStdString(out_dir);
        pdfHtmlDir_ = tempDir_.isValid() ? tempDir_.filePath("pdf-html")
                                         : QDir::tempPath() + "/imsg-pdf-html";
        QDir().mkpath(pdfHtmlDir_);
        out_dir = pdfHtmlDir_.toStdString();
    }

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

    setBusy(true);
    watcher_.setFuture(QtConcurrent::run([db_path, out_dir, fmt, opts]() {
        return imsg::export_database(db_path, out_dir, fmt, opts);
    }));
}

void MainWindow::onProgress(int done, int total) {
    status_->setText(QString("Exporting… %1 of %2 conversations").arg(done).arg(total));
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
        ? buttons->addButton("Open Settings", QDialogButtonBox::ActionRole)
        : nullptr;
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(copyBtn, &QPushButton::clicked, this,
            [error] { QApplication::clipboard()->setText(error); });
    connect(logBtn, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(logFilePath_)); });
    if (settingsBtn)
        connect(settingsBtn, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(
                "x-apple.systempreferences:com.apple.preference.security?"
                "Privacy_AllFiles"));
        });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

void MainWindow::exportFinished() {
    imsg::set_log_sink(nullptr);
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
        return;
    }

    QString msg = QString("Exported %1 conversation(s)").arg(s.conversations);
    if (s.attachments_copied > 0)
        msg += QString(", %1 attachment(s)").arg(s.attachments_copied);
    msg += ".";
    status_->setText(msg);
    openBtn_->setEnabled(true);
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
    s.setValue("ui/output", outputDir_->text());
    s.setValue("ui/me", meLabel_->text());
    s.setValue("ui/sinceOn", sinceOn_->isChecked());
    s.setValue("ui/since", since_->date().toString("yyyy-MM-dd"));
    s.setValue("ui/untilOn", untilOn_->isChecked());
    s.setValue("ui/until", until_->date().toString("yyyy-MM-dd"));
    s.setValue("ui/combined", combined_->isChecked());
    s.setValue("ui/copy", copyAttachments_->isChecked());
    s.setValue("ui/embed", embedAttachments_->isChecked());
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
    outputDir_->setText(s.value("ui/output", outputDir_->text()).toString());
    meLabel_->setText(s.value("ui/me", "Me").toString());
    sinceOn_->setChecked(s.value("ui/sinceOn", false).toBool());
    untilOn_->setChecked(s.value("ui/untilOn", false).toBool());
    const QDate sd = QDate::fromString(s.value("ui/since").toString(), "yyyy-MM-dd");
    if (sd.isValid()) since_->setDate(sd);
    const QDate ud = QDate::fromString(s.value("ui/until").toString(), "yyyy-MM-dd");
    if (ud.isValid()) until_->setDate(ud);
    combined_->setChecked(s.value("ui/combined", false).toBool());
    copyAttachments_->setChecked(s.value("ui/copy", false).toBool());
    embedAttachments_->setChecked(s.value("ui/embed", false).toBool());
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

    QStringList people;
    try {
        imsg::MessagesDatabase db(db_path, opts.me_label);
        if (!book.empty()) db.set_contacts(&book);
        db.open();
        QSet<QString> seen;
        for (const imsg::Chat& c : db.load_chat_index()) {
            if (c.message_count == 0) continue;
            for (const std::string& p : c.participants) {
                QString q = QString::fromStdString(p);
                if (!q.isEmpty() && !seen.contains(q)) { seen.insert(q); people << q; }
            }
        }
    } catch (const imsg::DatabaseError& e) {
        showExportError(QString::fromUtf8(e.what()), "Cannot read messages");
        return;
    }
    people.sort(Qt::CaseInsensitive);

    QDialog dlg(this);
    dlg.setWindowTitle("Select people to export");
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Export only conversations with the checked people "
                                 "(none checked = all):",
                                 &dlg));
    auto* list = new QListWidget(&dlg);
    for (const QString& p : people) {
        auto* item = new QListWidgetItem(p, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectedPeople_.contains(p) ? Qt::Checked : Qt::Unchecked);
    }
    list->setMinimumSize(420, 320);
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    selectedPeople_.clear();
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            selectedPeople_ << list->item(i)->text();
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

    auto* idField = new QLineEdit(&dlg);
    idField->setPlaceholderText("you@icloud.com");
    auto* pwField = new QLineEdit(&dlg);
    pwField->setEchoMode(QLineEdit::Password);
    pwField->setPlaceholderText("xxxx-xxxx-xxxx-xxxx");
    form->addRow("Apple ID:", idField);
    form->addRow("App password:", pwField);

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
    if (!tempDir_.isValid()) {
        status_->setText("Could not create a temporary file for the contacts.");
        return;
    }
    const QString path = tempDir_.filePath("icloud-contacts.vcf");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "iCloud import",
                             "Could not save the imported contacts.");
        return;
    }
    out.write(result.vcards.toUtf8());
    out.close();

    contacts_->setCurrentIndex(CtFile);  // "Contacts file…"
    contactsPath_->setText(path);
    onContactsChanged();
    status_->setText("Imported iCloud contacts; they will be used for name resolution.");
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

void MainWindow::runUpdateCheck(bool manual) {
    manualUpdateCheck_ = manual;
    if (manual) status_->setText("Checking for updates…");
    updater_->checkForUpdates();
}

void MainWindow::showAbout() {
    richTextDialog(
        this, "About iMessage Exporter",
        "<h3>iMessage Exporter " IMSG_VERSION "-" IMSG_BUILD_STAMP "</h3>"
        "<p>Export macOS iMessage / SMS history to TXT, JSON, or HTML.</p>"
        "<p>A small, fast C++ tool with a Qt desktop front-end, sharing one "
        "export engine across the CLI, desktop, and iOS.</p>"
        "<p><a href=\"https://github.com/grioghar/imessage-exporter-redux\">"
        "github.com/grioghar/imessage-exporter-redux</a><br>MIT License.</p>");
}
