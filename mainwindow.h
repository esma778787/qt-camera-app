#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QFileDialog>          // QFileDialog::Options
#include <QProcessEnvironment>  // makePythonEnv() dönüş tipi
#include <QStandardPaths>       // projectRoot() için

QT_BEGIN_NAMESPACE
namespace Ui { class btnLoadClasses; }  // .ui içindeki <class>btnLoadClasses</class> ile eşleşir
QT_END_NAMESPACE

// İleri bildirimler
class QCamera;
class QMediaCaptureSession;
class QImageCapture;
class QVideoWidget;
class QFrame;
class QLabel;
class QProcess;
class QResizeEvent;

class QListWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;
class QDockWidget;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    // Çekim
    void takeOne();
    void burstStart();
    void burstStop();
    void nextBurstShot();

    // Yol seçiciler
    void chooseDir();
    void chooseImagesDir();
    void chooseClassesFile();
    void chooseLabelsDir();

    // Eğitim / Tahmin / Labeling / LabelImg
    void trainStart();
    void loadModel();
    void predict();
    void startLabeling();
    void on_dockAnnotator_destroyed();
    void openAnnotatorInNewWindow();

    // Annotator log yakalama (opsiyonel)
    void onAnnotatorInfo(const QString& msg);
    void onAnnotatorLog (const QString& line);

    // Auto-connect slotları (buton tıklamaları)
    void on_btnStartLabel_clicked();
    void on_btnStartLabelImg_clicked();
    void on_btnCompareLI_clicked();                 // ← yalnız deklarasyon

    // --- Karşılaştırma yardımcı slotu ---
    void compareCurrentImageAgainstLabelImg();      // ← yalnız deklarasyon

private:
    // -------- Yardımcılar --------
    QString makeSavePath() const;
    QString makeFileName() const;
    QString classDir() const;
    void    populateLabelsFromDir();
    void    startInferProcess(const QString& imagePath);
    int     countLabelFiles(const QString& dirPath, const QStringList& exts) const;
    void    updateLabelCount();

    // ====== Dosya diyalogları (SAFE) — SADECE ana imzalar ======
    QString     getExistingDirectorySafe(
        QWidget* parent,
        const QString& title,
        const QString& dir,
        QFileDialog::Options options = QFileDialog::Options()
        ) const;

    QString     getOpenFileNameSafe(
        QWidget* parent,
        const QString& title,
        const QString& dir,
        const QString& filter = QString(),
        QFileDialog::Options options = QFileDialog::Options()
        ) const;

    QStringList getOpenFileNamesSafe(
        QWidget* parent,
        const QString& title,
        const QString& dir,
        const QString& filter = QString(),
        QFileDialog::Options options = QFileDialog::Options()
        ) const;

    // ====== Görsel listeleri ======
    QStringList listImagesCaseInsensitive(const QString& dirPath) const;
    QStringList loadImageListTxt(const QString& listTxtPath) const;

    // ====== Python / Proje yolları ve ortam ======
    QString projectRoot() const;
    QString venvPythonPath() const;
    QString scriptPath(const QString& name) const;
    QProcessEnvironment makePythonEnv() const;

    // LabelImg başlatma (REPO / non-detached, WD=repoDir)
    void    launchLabelImg(const QString& imagesDir,
                        const QString& classesPath,
                        const QString& format,
                        const QString& saveDir);

    // Dock ↔ merkez taşıma
    void enterAnnotatorFull();
    void exitAnnotatorFull();

    // VideoHost genişlik kilidi
    void lockVideoWidth(int w);
    void unlockVideoWidth();
    int  currentVideoWidth() const;

private:
    Ui::btnLoadClasses *ui = nullptr;  // .ui değişmediği için bu isim

    // Üyeler
    QCamera*               m_camera = nullptr;
    QMediaCaptureSession*  m_capture = nullptr;
    QImageCapture*         m_imageCap = nullptr;
    QVideoWidget*          m_videoWidget = nullptr;

    QFrame*  m_previewBox = nullptr;
    QLabel*  m_dirLabel   = nullptr;
    QLabel*  m_preview    = nullptr;

    QString  m_saveDir;
    QString  m_modelPath;
    QString  m_lastSavedPath;
    bool     m_runInferWhenSaved = false;

    // Burst
    QTimer&  burstTimer() { return m_burstTimer; }
    QTimer   m_burstTimer;
    int      m_burstIntervalMs = 250;
    bool     m_burstActive = false;
    int      m_burstTargetCount = 200;
    int      m_burstTaken = 0;

    // Prosesler
    QProcess* m_trainProc = nullptr;
    QProcess* m_inferProc = nullptr;

    // Sayaç
    QTimer    m_labelCountTimer;

    // Video genişlik kilidi durumu
    int       m_lockedVideoW = -1;   // -1: kilit yok, >=0: kilitli genişlik (px)
};
