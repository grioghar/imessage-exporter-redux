#include "main_window.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <ctime>
#include <string>

#include "imsg/backup.hpp"
#include "imsg/database.hpp"
#include "imsg/exporters.hpp"
#include "imsg/log.hpp"
#include "imsg/time_util.hpp"

namespace {
// Index conventions for the source / contacts combo boxes.
enum Source { SrcAuto = 0, SrcFile = 1, SrcBackup = 2 };
enum Contacts { CtNone = 0, CtThisMac = 1, CtFile = 2, CtBackup = 3 };
}  // namespace

MainWindow::MainWindow()
    : logMutex_(std::make_shared<std::mutex>()),
      logBuffer_(std::make_shared<std::vector<std::string>>()) {
    setWindowTitle("iMessage Exporter");

    auto* form = new QFormLayout;

    // --- Source -------------------------------------------------------------
    source_ = new QComboBox;
    source_->addItem("Auto-detect Messages on this Mac");
    source_->addItem("Database file (chat.db / sms.db)…");
    source_->addItem("Device backup…");
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

    // --- Options ------------------------------------------------------------
    format_ = new QComboBox;
    format_->addItems({"txt", "json", "html"});
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
    auto* flags = new QHBoxLayout;
    flags->addWidget(combined_);
    flags->addWidget(copyAttachments_);
    flags->addStretch();
    form->addRow("", flags);

    contacts_ = new QComboBox;
    contacts_->addItem("None (show phone numbers / emails)");
    contacts_->addItem("This Mac's Contacts");
    contacts_->addItem("Contacts file (.abcddb / .vcf)…");
    contacts_->addItem("From the selected backup");
    form->addRow("Names:", contacts_);

    auto* ctRow = new QHBoxLayout;
    contactsPath_ = new QLineEdit;
    contactsPath_->setPlaceholderText("Path to .abcddb or .vcf");
    contactsBrowse_ = new QPushButton("Browse…");
    ctRow->addWidget(contactsPath_);
    ctRow->addWidget(contactsBrowse_);
    form->addRow("Contacts file:", ctRow);

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

    auto* root = new QVBoxLayout(this);
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
    connect(&watcher_, &QFutureWatcher<imsg::ExportSummary>::finished, this,
            &MainWindow::exportFinished);

    onSourceChanged();
    onContactsChanged();
}

void MainWindow::onSourceChanged() {
    const int s = source_->currentIndex();
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
    if (!imsg::parse_format(format_->currentText().toStdString(), fmt)) {
        error = "Unknown format.";
        return false;
    }

    opts.me_label = meLabel_->text().isEmpty() ? "Me" : meLabel_->text().toStdString();
    opts.combined = combined_->isChecked();
    opts.copy_attachments = copyAttachments_->isChecked();

    if (sinceOn_->isChecked() &&
        imsg::parse_date(since_->date().toString("yyyy-MM-dd").toStdString(), opts.since))
        opts.has_since = true;
    if (untilOn_->isChecked() &&
        imsg::parse_date(until_->date().toString("yyyy-MM-dd").toStdString(),
                         opts.until, /*end_of_day=*/true))
        opts.has_until = true;

    const int src = source_->currentIndex();
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
        default:
            break;
    }
    return true;
}

void MainWindow::setBusy(bool busy) {
    exportBtn_->setEnabled(!busy);
    status_->setText(busy ? "Exporting…" : "Ready.");
}

void MainWindow::startExport() {
    std::string db_path, out_dir;
    imsg::Format fmt = imsg::Format::Text;
    imsg::ExportOptions opts;
    QString error;
    if (!buildInputs(db_path, out_dir, fmt, opts, error)) {
        QMessageBox::warning(this, "Cannot export", error);
        return;
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

    auto buf = logBuffer_;
    auto mtx = logMutex_;
    imsg::set_log_sink([buf, mtx](imsg::LogLevel level, const std::string& msg) {
        std::lock_guard<std::mutex> lk(*mtx);
        buf->push_back(std::string("[") + imsg::log_level_name(level) + "] " + msg);
    });

    setBusy(true);
    watcher_.setFuture(QtConcurrent::run([db_path, out_dir, fmt, opts]() {
        return imsg::export_database(db_path, out_dir, fmt, opts);
    }));
}

void MainWindow::exportFinished() {
    imsg::set_log_sink(nullptr);
    {
        std::lock_guard<std::mutex> lk(*logMutex_);
        for (const std::string& line : *logBuffer_)
            logView_->appendPlainText(QString::fromStdString(line));
        logBuffer_->clear();
    }

    const imsg::ExportSummary s = watcher_.result();
    setBusy(false);
    if (!s.ok) {
        status_->setText("Failed.");
        QMessageBox::critical(this, "Export failed", QString::fromStdString(s.error));
        return;
    }
    if (s.conversations == 0) {
        status_->setText("No conversations matched.");
        QMessageBox::information(this, "Nothing exported",
                                 "No conversations with messages were found "
                                 "(check the date range or source).");
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
