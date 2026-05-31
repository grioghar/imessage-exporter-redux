// Qt Widgets front-end for the exporter. Thin shell over the imsg_db engine:
// it gathers options, runs export_database() off the UI thread, and shows the
// result. The same binary builds a macOS .app, a Windows .exe, and a Linux app.
#pragma once

#include <QFutureWatcher>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>
#include <mutex>
#include <vector>

#include "icloud_contacts.hpp"
#include "imsg/export_job.hpp"

class QComboBox;
class QLineEdit;
class QCheckBox;
class QDateEdit;
class QPushButton;
class QPlainTextEdit;
class QLabel;
class QWidget;

class MainWindow : public QWidget {
    Q_OBJECT

   public:
    MainWindow();

   private slots:
    void onSourceChanged();
    void onContactsChanged();
    void browseDatabase();
    void browseOutput();
    void browseContacts();
    void startExport();
    void exportFinished();
    void openOutputDir();
    void importICloudContacts();
    void icloudFinished();
    void showHowToGetData();
    void showAbout();

   private:
    // Validates the form and fills the engine inputs; returns false (+ message)
    // when something is wrong (no source chosen, encrypted backup, etc.).
    bool buildInputs(std::string& db_path, std::string& out_dir, imsg::Format& fmt,
                     imsg::ExportOptions& opts, QString& error);
    void setBusy(bool busy);

    // Source selection.
    QComboBox* source_ = nullptr;       // auto / file / backup
    QLineEdit* dbPath_ = nullptr;
    QPushButton* dbBrowse_ = nullptr;
    QComboBox* backup_ = nullptr;

    // Options.
    QComboBox* format_ = nullptr;
    QLineEdit* outputDir_ = nullptr;
    QLineEdit* meLabel_ = nullptr;
    QCheckBox* sinceOn_ = nullptr;
    QDateEdit* since_ = nullptr;
    QCheckBox* untilOn_ = nullptr;
    QDateEdit* until_ = nullptr;
    QCheckBox* combined_ = nullptr;
    QCheckBox* copyAttachments_ = nullptr;
    QComboBox* contacts_ = nullptr;     // none / this Mac / file / from backup
    QLineEdit* contactsPath_ = nullptr;
    QPushButton* contactsBrowse_ = nullptr;
    QPushButton* icloudBtn_ = nullptr;
    QComboBox* logLevel_ = nullptr;

    QPushButton* exportBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;

    QFutureWatcher<imsg::ExportSummary> watcher_;
    QFutureWatcher<icloud::Result> icloudWatcher_;
    std::shared_ptr<std::mutex> logMutex_;
    std::shared_ptr<std::vector<std::string>> logBuffer_;
    QTemporaryDir tempDir_;  // holds files extracted from a backup
    QString lastOutputDir_;
};
