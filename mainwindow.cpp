// === CORE ===
#include "mainwindow.h"
#include "annotatorwidget.h"
#include "ui_mainwindow.h"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QVideoWidget>

#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QStatusBar>
#include <QFileDialog>
#include <QLabel>
#include <QPixmap>
#include <QSizePolicy>
#include <QFrame>
#include <QResizeEvent>
#include <QTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMessageBox>
#include <QFile>
#include <algorithm>
#include <QDockWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QTextStream>
#include <QToolButton>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QPointer>
#include <QDoubleSpinBox>

#include <QSet>
#include <QRegularExpression>
#include <cmath>

// ================================
//  EK: Batch tahmin kuyruğu
// ================================
static QStringList g_predQueue;

// ================================
//  YOL & ORTAM YARDIMCILARI (Artık üye fonksiyonlar)
// ================================
QString MainWindow::projectRoot() const
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(QDir::homePath() + "/Documents/CameraMenuApp");
#else
    return QDir::cleanPath(QDir::homePath() + "/CameraMenuApp");
#endif
}

QString MainWindow::venvPythonPath() const
{
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(projectRoot() + "/venv/Scripts/python.exe");
#else
    return QStringLiteral("python3");
#endif
}

QString MainWindow::scriptPath(const QString& rel) const
{
    return QDir::toNativeSeparators(projectRoot() + "/" + rel);
}

QProcessEnvironment MainWindow::makePythonEnv() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("PYTHONHOME");
    env.remove("PYTHONPATH");

    const QString venvDir    = QDir::toNativeSeparators(projectRoot() + "/venv");
    const QString scriptsDir = QDir::toNativeSeparators(venvDir + "/Scripts");
#ifdef Q_OS_WIN
    const QString libsDir    = QDir::toNativeSeparators(venvDir + "/Library/bin");
#endif

    QString path = env.value("PATH");
#ifdef Q_OS_WIN
    env.insert("PATH", scriptsDir + ";" + libsDir + ";" + path);
#else
    env.insert("PATH", scriptsDir + ":" + path);
#endif

    env.insert("PYTHONPATH", QDir::toNativeSeparators(projectRoot() + "/python"));
    env.insert("VIRTUAL_ENV", venvDir);
    env.insert("PYTHONIOENCODING", "utf-8");
    return env;
}

// ---------- Güvenli QFileDialog yardımcıları ----------
QString MainWindow::getExistingDirectorySafe(QWidget* parent, const QString& title,
                                             const QString& startDir,
                                             QFileDialog::Options extra) const
{
    QFileDialog dlg(parent, title, startDir);
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    dlg.setOption(QFileDialog::DontResolveSymlinks, true);
    dlg.setOptions(dlg.options() | QFileDialog::DontUseNativeDialog | extra);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
        return dlg.selectedFiles().first();
    return {};
}

QString MainWindow::getOpenFileNameSafe(QWidget* parent, const QString& title,
                                        const QString& startDir,
                                        const QString& filter,
                                        QFileDialog::Options extra) const
{
    QFileDialog dlg(parent, title, startDir, filter);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setOptions(dlg.options() | QFileDialog::DontUseNativeDialog | extra);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
        return dlg.selectedFiles().first();
    return {};
}

QStringList MainWindow::getOpenFileNamesSafe(QWidget* parent, const QString& title,
                                             const QString& startDir,
                                             const QString& filter,
                                             QFileDialog::Options extra) const
{
    QFileDialog dlg(parent, title, startDir, filter);
    dlg.setFileMode(QFileDialog::ExistingFiles);
    dlg.setOptions(dlg.options() | QFileDialog::DontUseNativeDialog | extra);
    if (dlg.exec() == QDialog::Accepted)
        return dlg.selectedFiles();
    return {};
}
// ------------------------------------------------------

QStringList MainWindow::listImagesCaseInsensitive(const QString& dir) const
{
    QStringList out;
    QDir d(dir);
    const QStringList files = d.entryList(QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString ext = QFileInfo(f).suffix().toLower();
        if (QStringList{"png","jpg","jpeg","bmp","tif","tiff"}.contains(ext))
            out << d.absoluteFilePath(f);
    }
    return out;
}

QStringList MainWindow::loadImageListTxt(const QString& txtFile) const
{
    QStringList out;
    QFile f(txtFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;

    const QDir base(QFileInfo(txtFile).absolutePath());
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QString path = QDir::isAbsolutePath(line) ? line : base.filePath(line);
        path = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());

        const QString ext = QFileInfo(path).suffix().toLower();
        if (QFileInfo::exists(path) && QStringList{"png","jpg","jpeg","bmp","tif","tiff"}.contains(ext))
            out << path;
    }
    f.close();

    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b){
        return QFileInfo(a).fileName().toLower() < QFileInfo(b).fileName().toLower();
    });

    return out;
}

// ================================
//  YARDIMCI: Etiket klasörü (base/label)
// ================================
QString MainWindow::classDir() const {
    const QString base = makeSavePath();
    if (!ui->cmbLabel) return base;
    const QString label = ui->cmbLabel->currentText().trimmed();
    if (label.isEmpty()) return base;

    QDir d(base);
    d.mkpath(label);
    return d.filePath(label);
}

// ================================
//  YARDIMCI: Kökün alt klasörlerinden cmbLabel doldur
// ================================
void MainWindow::populateLabelsFromDir() {
    if (!ui->cmbLabel) return;
    ui->cmbLabel->clear();

    QDir base(makeSavePath());
    const auto dirs = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &inf : dirs)
        ui->cmbLabel->addItem(inf.fileName());

    if (ui->cmbLabel->count() == 0)
        ui->cmbLabel->addItems(QStringList() << "person" << "background");
}

// ================================
//  CTOR
// ================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::btnLoadClasses)   // .ui sınıfı
{
    ui->setupUi(this);

    // ---- Dock Annotator
    if (ui->dockAnnotator) {
        ui->dockAnnotator->hide();
        ui->dockAnnotator->setAllowedAreas(Qt::AllDockWidgetAreas);
        if (dockWidgetArea(ui->dockAnnotator) != Qt::RightDockWidgetArea) {
            addDockWidget(Qt::RightDockWidgetArea, ui->dockAnnotator);
        }
    }

    if (ui->txtPredLog) {
        ui->txtPredLog->setReadOnly(true);
        ui->txtPredLog->setPlainText("Log hazır.");
    }

    Q_ASSERT_X(ui->menuFrame, "MainWindow", "menuFrame yok");
    Q_ASSERT_X(ui->videoHost, "MainWindow", "videoHost yok");
    Q_ASSERT_X(ui->horizontalLayout, "MainWindow", "horizontalLayout yok");

    // ---- Splitter
    auto *root = ui->horizontalLayout;
    root->removeWidget(ui->videoHost);
    root->removeWidget(ui->menuFrame);

    QSplitter *split = new QSplitter(Qt::Horizontal, this);
    split->setObjectName("rootSplit");
    split->setChildrenCollapsible(false);
    split->setHandleWidth(6);
    split->addWidget(ui->videoHost);
    split->addWidget(ui->menuFrame);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({800, 320});
    root->addWidget(split);

    ui->menuFrame->setMinimumWidth(300);
    ui->menuFrame->setStyleSheet("background:#1b1b1b; border:1px solid #333;");

    QWidget* host = static_cast<QWidget*>(ui->videoHost);
    host->setStyleSheet("background:#111; border:1px dashed #444;");

    // ---- Video widget
    m_videoWidget = new QVideoWidget(host);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (host->layout()) {
        host->layout()->addWidget(m_videoWidget);
    } else {
        auto *vl = new QVBoxLayout(host);
        vl->setContentsMargins(0,0,0,0);
        vl->addWidget(m_videoWidget);
        host->setLayout(vl);
    }
    m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);

    // ---- Sağ panelde mini önizleme
    m_previewBox = new QFrame(ui->menuFrame);
    m_previewBox->setObjectName("previewBox");
    m_previewBox->setFrameShape(QFrame::StyledPanel);
    auto *pv = new QVBoxLayout(m_previewBox);
    pv->setContentsMargins(6,6,6,6);
    pv->setSpacing(6);

    m_dirLabel = new QLabel(m_previewBox);
    m_dirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_dirLabel->setStyleSheet("color:#bbb; font-size:12px;");
    pv->addWidget(m_dirLabel);

    m_preview = new QLabel(m_previewBox);
    m_preview->setMinimumSize(220,160);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setText("Önizleme yok");
    m_preview->setScaledContents(true);
    pv->addWidget(m_preview, 1);
    m_previewBox->lower();

    // ---- Kamera set-up
    const auto devs = QMediaDevices::videoInputs();
    QCameraDevice dev = QMediaDevices::defaultVideoInput();
    if (dev.isNull() && !devs.isEmpty())
        dev = devs.first();

    m_camera   = new QCamera(dev, this);
    m_capture  = new QMediaCaptureSession(this);
    m_imageCap = new QImageCapture(this);

    m_capture->setCamera(m_camera);
    m_capture->setVideoOutput(m_videoWidget);
    m_capture->setImageCapture(m_imageCap);

    connect(m_camera, &QCamera::activeChanged, this, [this](bool a){
        if (statusBar()) statusBar()->showMessage(a ? "Kamera aktif" : "Kamera durdu", 2000);
    });

    m_burstTimer.setInterval(m_burstIntervalMs);
    connect(&m_burstTimer, &QTimer::timeout, this, &MainWindow::nextBurstShot);

    connect(m_imageCap, &QImageCapture::imageCaptured, this,
            [this](int, const QImage& img){
                if (m_preview) m_preview->setPixmap(QPixmap::fromImage(img));
            });

    connect(m_imageCap, &QImageCapture::imageSaved, this,
            [this](int, const QString& path){
                m_lastSavedPath = path;
                if (statusBar()) statusBar()->showMessage("Kaydedildi: " + path, 3000);
                if (m_preview && m_preview->pixmap(Qt::ReturnByValue).isNull()) {
                    QPixmap px(path);
                    if (!px.isNull()) m_preview->setPixmap(px);
                }
                if (m_runInferWhenSaved) {
                    m_runInferWhenSaved = false;
                    startInferProcess(path);
                }
            });

    // ---- Buton bağları (mevcut)
    Q_ASSERT_X(ui->btnSnap,        "MainWindow", "btnSnap yok");
    Q_ASSERT_X(ui->btnBurstStart,  "MainWindow", "btnBurstStart yok");
    Q_ASSERT_X(ui->btnBurstStop,   "MainWindow", "btnBurstStop yok");
    Q_ASSERT_X(ui->btnChooseDir,   "MainWindow", "btnChooseDir yok");

    connect(ui->btnSnap,       &QPushButton::clicked, this, &MainWindow::takeOne);
    connect(ui->btnBurstStart, &QPushButton::clicked, this, &MainWindow::burstStart);
    connect(ui->btnBurstStop,  &QPushButton::clicked, this, &MainWindow::burstStop);
    connect(ui->btnChooseDir,  &QPushButton::clicked, this, &MainWindow::chooseDir);

    if (ui->btnTrain)
        connect(ui->btnTrain, &QPushButton::clicked, this, &MainWindow::trainStart);

    Q_ASSERT_X(ui->btnLoadModel, "MainWindow", "btnLoadModel yok");
    Q_ASSERT_X(ui->btnPredict,   "MainWindow", "btnPredict yok");
    connect(ui->btnLoadModel, &QPushButton::clicked, this, &MainWindow::loadModel);
    connect(ui->btnPredict,   &QPushButton::clicked, this, &MainWindow::predict);

    // ---- Batch Tahmin: sağ tık menüsü
    if (ui->btnPredict) {
        ui->btnPredict->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->btnPredict, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pos){
            QMenu menu(this);
            QAction* actFromDir   = menu.addAction(tr("Klasörden Tahmin (Batch)"));
            QAction* actFromMulti = menu.addAction(tr("Çoklu Dosyadan Tahmin (Batch)"));
            QAction* chosen = menu.exec(ui->btnPredict->mapToGlobal(pos));
            if (!chosen) return;

            if (chosen == actFromDir) {
                const QString dir = getExistingDirectorySafe(
                    this, tr("Tahmin için klasör seç"),
                    m_saveDir.isEmpty() ? QDir::homePath() : m_saveDir);
                if (dir.isEmpty()) return;

                QStringList imgs = listImagesCaseInsensitive(dir);
                if (imgs.isEmpty()) {
                    QMessageBox::information(this, tr("Boş klasör"), tr("Bu klasörde görsel yok."));
                    return;
                }
                g_predQueue = imgs;
                if (ui->txtPredLog)
                    ui->txtPredLog->appendPlainText(tr("Batch başladı (%1 görsel).").arg(g_predQueue.size()));
                startInferProcess(g_predQueue.takeFirst());
            } else if (chosen == actFromMulti) {
                QStringList chosenFiles = getOpenFileNamesSafe(
                    this, tr("Tahmin için görselleri seç"),
                    m_saveDir.isEmpty() ? QDir::homePath() : m_saveDir,
                    tr("Görseller (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Tümü (*.*)"));
                if (chosenFiles.isEmpty()) return;

                std::sort(chosenFiles.begin(), chosenFiles.end(),
                          [](const QString& a, const QString& b){
                              return QFileInfo(a).fileName().toLower()
                              < QFileInfo(b).fileName().toLower();
                          });

                g_predQueue = chosenFiles;
                if (ui->txtPredLog)
                    ui->txtPredLog->appendPlainText(tr("Batch başladı (%1 görsel).").arg(g_predQueue.size()));
                startInferProcess(g_predQueue.takeFirst());
            }
        });
    }

    // ---- Tahmin bölümüne iki buton
    if (ui->btnPredFromDir) {
        connect(ui->btnPredFromDir, &QPushButton::clicked, this, [this]{
            if (m_modelPath.isEmpty()) { QMessageBox::warning(this, tr("Model yok"), tr("Önce modeli yükleyin.")); return; }

            const QString dir = getExistingDirectorySafe(
                this, tr("Tahmin için klasör seç"),
                m_saveDir.isEmpty() ? QDir::homePath() : m_saveDir);
            if (dir.isEmpty()) return;

            QStringList imgs = listImagesCaseInsensitive(dir);
            if (imgs.isEmpty()) {
                QMessageBox::information(this, tr("Boş klasör"), tr("Bu klasörde görsel yok."));
                return;
            }
            g_predQueue = imgs;
            if (ui->txtPredLog)
                ui->txtPredLog->appendPlainText(tr("Batch başladı (%1 görsel) [Klasör].").arg(g_predQueue.size()));
            startInferProcess(g_predQueue.takeFirst());
        });
    }

    if (ui->btnPredFromList) {
        connect(ui->btnPredFromList, &QPushButton::clicked, this, [this]{
            if (m_modelPath.isEmpty()) { QMessageBox::warning(this, tr("Model yok"), tr("Önce modeli yükleyin.")); return; }

            const QString txt = getOpenFileNameSafe(
                this, tr("Görüntü listesi (.txt) seç"),
                m_saveDir.isEmpty() ? QDir::homePath() : m_saveDir,
                tr("Text list (*.txt);;All Files (*.*)"));
            if (txt.isEmpty()) return;

            QStringList imgs = loadImageListTxt(txt);
            if (imgs.isEmpty()) {
                QMessageBox::information(this, tr("Liste boş"), tr("Dosyada geçerli görüntü yolu bulunamadı."));
                return;
            }
            g_predQueue = imgs;
            if (ui->txtPredLog)
                ui->txtPredLog->appendPlainText(tr("Batch başladı (%1 görsel) [Liste].").arg(g_predQueue.size()));
            startInferProcess(g_predQueue.takeFirst());
        });
    }

    // ---- Labellama (LabelImg) – core, sadece bağlar
    if (ui->cbFormat)      ui->cbFormat->setCurrentText("YOLO");
    if (ui->lblLabelCount) ui->lblLabelCount->setText("Etiket dosyası: 0");

    connect(ui->btnPickImages,  &QPushButton::clicked, this, &MainWindow::chooseImagesDir);
    connect(ui->btnPickClasses, &QPushButton::clicked, this, &MainWindow::chooseClassesFile);
    connect(ui->btnPickLabels,  &QPushButton::clicked, this, &MainWindow::chooseLabelsDir);

    connect(ui->cbFormat, &QComboBox::currentTextChanged, this, [this](const QString& t){
        if (!m_saveDir.isEmpty() && ui->leLabels) {
            QDir root(m_saveDir);
            const bool isVOC = (t == "PascalVOC");
            ui->leLabels->setText(QDir::toNativeSeparators(
                root.filePath(isVOC ? "labels_voc/train" : "labels_yolo/train")
                ));
        }
        updateLabelCount();
        if (ui->annotView) ui->annotView->setFormat(t);
    });

    if (ui->leLabels) {
        connect(ui->leLabels, &QLineEdit::textChanged, this, [this](const QString& s){
            if (ui->annotView) ui->annotView->setSaveDir(s.trimmed());
        });
    }

    connect(&m_labelCountTimer, &QTimer::timeout, this, &MainWindow::updateLabelCount);
    m_labelCountTimer.start(1000);

    if (m_dirLabel) m_dirLabel->setText("Kayıt klasörü (dataset kökü): " + makeSavePath());
    populateLabelsFromDir();

    m_camera->start();

    // =================================================================
    //  FULLSCREEN ANNOTATOR HOST: Dock içeriğini merkeze taşıma
    // =================================================================
    QWidget *annotFullHost = nullptr;
    if (ui->centralwidget) {
        annotFullHost = this->findChild<QWidget*>("annotFullHost");
        if (!annotFullHost) {
            annotFullHost = new QWidget(ui->centralwidget);
            annotFullHost->setObjectName("annotFullHost");
            annotFullHost->setAttribute(Qt::WA_StyledBackground, true);
            annotFullHost->setStyleSheet("#annotFullHost { background:#111; }");
            annotFullHost->hide();

            auto *annotFullLayout = new QVBoxLayout(annotFullHost);
            annotFullLayout->setContentsMargins(0,0,0,0);
            annotFullLayout->setSpacing(0);
        }
    }

    if (ui->annotView && ui->dockAnnotator && annotFullHost) {
        ui->annotView->setHosts(ui->dockAnnotator, annotFullHost, ui->menuFrame, ui->videoHost);

        if (ui->txtLog) {
            connect(ui->annotView, &AnnotatorWidget::log,  ui->txtLog, &QPlainTextEdit::appendPlainText);
            connect(ui->annotView, &AnnotatorWidget::info, ui->txtLog, &QPlainTextEdit::appendPlainText);
        }
    }

    if (ui->annotView) {
        connect(ui->annotView, &AnnotatorWidget::boxesChanged,
                this, [this](const QVector<AnnotatorWidget::Box>& boxes, const QString& stem){
                    if (ui->listBoxes) {
                        ui->listBoxes->clear();
                        for (int i = 0; i < boxes.size(); ++i) {
                            const auto& b = boxes[i];
                            ui->listBoxes->addItem(
                                QString::asprintf("#%d  cls=%d  [%.1f, %.1f, %.1f, %.1f]",
                                                  i+1, b.cls,
                                                  b.rect.x(), b.rect.y(),
                                                  b.rect.width(), b.rect.height())
                                );
                        }
                    }
                    if (ui->lblBoxes) {
                        ui->lblBoxes->setText(QString("%1: %2 kutu").arg(stem).arg(boxes.size()));
                    }
                });
    }

    // --- Dock↔Full geçiş butonları
    if (ui->btnOpenAnnotator && ui->annotView) {
        connect(ui->btnOpenAnnotator, &QPushButton::clicked, this, [this]{
            if (!ui->annotView) return;
            if (ui->cbFormat) ui->annotView->setFormat(ui->cbFormat->currentText());
            if (ui->leLabels) ui->annotView->setSaveDir(ui->leLabels->text().trimmed());

            QString cls;
            if (ui->chkUseDefault && ui->chkUseDefault->isChecked() && ui->txtDefaultClass)
                cls = ui->txtDefaultClass->text().trimmed();
            else if (ui->cmbClass)
                cls = ui->cmbClass->currentText().trimmed();
            if (!cls.isEmpty()) ui->annotView->setActiveClass(cls);

            ui->annotView->enterFull();
        });
    }
    if (ui->btnBackToDock && ui->annotView) {
        connect(ui->btnBackToDock, &QPushButton::clicked, this, [this]{
            if (ui->annotView) ui->annotView->exitFull();
            if (ui->btnStartLabel) ui->btnStartLabel->setEnabled(true);
        });
    }

    // ======================================================
    //  Sağ panel "Classes / Files / Log" entegrasyonu
    // ======================================================

    if (ui->toolButton && ui->cmbClass) {
        connect(ui->toolButton, &QToolButton::clicked, this, [this]{
            const QString path = getOpenFileNameSafe(this, "classes.txt seç", {}, "Text (*.txt *.names)");
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

            QStringList classes;
            while (!f.atEnd()) {
                const QString s = QString::fromUtf8(f.readLine()).trimmed();
                if (!s.isEmpty()) classes << s;
            }
            f.close();

            ui->cmbClass->clear();
            ui->cmbClass->addItems(classes);

            if (ui->lblAnnotInfo)
                ui->lblAnnotInfo->setText(QString("Sınıflar yüklendi (%1)").arg(classes.size()));
            if (ui->txtLog)
                ui->txtLog->appendPlainText("Classes loaded from: " + path);
        });
    }

    if (ui->chkUseDefault && ui->txtDefaultClass) {
        ui->txtDefaultClass->setEnabled(ui->chkUseDefault->isChecked());
        connect(ui->chkUseDefault, &QCheckBox::toggled, this, [this](bool on){
            ui->txtDefaultClass->setEnabled(on);
        });
    }

    auto applyActiveClass = [this]{
        const QString cls = (ui->chkUseDefault && ui->chkUseDefault->isChecked())
        ? (ui->txtDefaultClass ? ui->txtDefaultClass->text().trimmed() : QString())
        : (ui->cmbClass ? ui->cmbClass->currentText().trimmed() : QString());
        if (!cls.isEmpty()) {
            if (ui->annotView) ui->annotView->setActiveClass(cls);
            if (ui->lblAnnotInfo) ui->lblAnnotInfo->setText("Aktif sınıf: " + cls);
            if (ui->txtLog) ui->txtLog->appendPlainText("Active class set: " + cls);
        }
    };
    if (ui->cmbClass)
        connect(ui->cmbClass, &QComboBox::currentTextChanged, this, [=]{ applyActiveClass(); });
    if (ui->txtDefaultClass)
        connect(ui->txtDefaultClass, &QLineEdit::textChanged, this, [=]{
            if (ui->chkUseDefault && ui->chkUseDefault->isChecked()) applyActiveClass();
        });
    if (ui->chkUseDefault)
        connect(ui->chkUseDefault, &QCheckBox::toggled, this, [=]{ applyActiveClass(); });

    // ==== Lifetime güvenli yerel lambda'lar ====
    QPointer<MainWindow> self(this);

    auto updateCounter = [self]{
        if (!self) return;
        auto ui = self->ui;
        if (!ui || !ui->lblCounter || !ui->listFiles) return;
        const int total = ui->listFiles->count();
        const int idx   = ui->listFiles->currentRow();
        ui->lblCounter->setText(QString("%1 / %2").arg(total ? idx+1 : 0).arg(total));
    };

    auto showImageForRow = [self](int row){
        if (!self) return;
        auto ui = self->ui;
        if (!ui || !ui->listFiles) return;
        if (row < 0 || row >= ui->listFiles->count()) return;

        QListWidgetItem* item = ui->listFiles->item(row);
        if (!item) return;
        QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) path = item->text();

        if (!path.isEmpty()) {
            if (ui->annotView) ui->annotView->loadImage(path);
            if (ui->lblAnnotInfo) ui->lblAnnotInfo->setText(QFileInfo(path).fileName() + " yüklendi");
            if (ui->txtLog) ui->txtLog->appendPlainText("Loaded image: " + path);
        }
    };

    if (ui->listFiles) {
        connect(ui->listFiles, &QListWidget::currentRowChanged, this, [=](int r){
            updateCounter();
            showImageForRow(r);
        });
    }

    auto loadImagesFromDirLambda = [this, &updateCounter, &showImageForRow](const QString& dir){
        if (!ui->listFiles) return;
        QDir d(dir);
        const QStringList exts = {"*.jpg","*.jpeg","*.png","*.bmp","*.tif","*.tiff",
                                  "*.JPG","*.JPEG","*.PNG","*.TIF","*.TIFF"};
        const QStringList files = d.entryList(exts, QDir::Files, QDir::Name);

        bool prev = ui->listFiles->blockSignals(true);
        ui->listFiles->clear();
        for (const QString& f : files) {
            auto* it = new QListWidgetItem(f);
            it->setData(Qt::UserRole, d.absoluteFilePath(f));
            ui->listFiles->addItem(it);
        }
        ui->listFiles->blockSignals(prev);

        if (!files.isEmpty()) {
            ui->listFiles->setCurrentRow(0);

            QPointer<QListWidget> list    = ui->listFiles;
            QPointer<QLabel>      counter = ui->lblCounter;
            QPointer<AnnotatorWidget> annot = ui->annotView;
            QPointer<QLabel>      info    = ui->lblAnnotInfo;
            QPointer<QPlainTextEdit> log  = ui->txtLog;

            QMetaObject::invokeMethod(this, [list, counter, annot, info, log]{
                if (!list) return;
                const int r = list->currentRow();

                if (counter) {
                    const int total = list->count();
                    counter->setText(QString("%1 / %2").arg(total ? r+1 : 0).arg(total));
                }

                if (r >= 0) {
                    if (QListWidgetItem* item = list->item(r)) {
                        QString path = item->data(Qt::UserRole).toString();
                        if (path.isEmpty()) path = item->text();
                        if (!path.isEmpty()) {
                            if (annot) annot->loadImage(path);
                            if (info)  info->setText(QFileInfo(path).fileName() + " yüklendi");
                            if (log)   log->appendPlainText("Loaded image: " + path);
                        }
                    }
                }
            }, Qt::QueuedConnection);
        } else {
            updateCounter();
        }

        if (ui->txtPredLog)
            ui->txtPredLog->appendPlainText(QString("Loaded %1 images from %2").arg(files.size()).arg(dir));
    };

    // Annotator hızlı komutlar
    AnnotatorWidget* annot = ui->annotView;
    if (!annot) {
        qDebug() << "Uyari: AnnotatorWidget (annotView) bulunamadı. Promote yapıldı mı?";
    } else {
        auto ensureSaveDir = [&](AnnotatorWidget* A){
            QString dir = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
            const QString fmt = ui->cbFormat ? ui->cbFormat->currentText() : QString("YOLO");
            const bool isVOC  = (fmt == "PascalVOC");

            if (dir.isEmpty()) {
                const QString img = A->currentImage();
                if (!img.isEmpty()) {
                    QDir base(QFileInfo(img).absolutePath());
                    dir = base.filePath(isVOC ? "labels_voc/train" : "labels_yolo/train");
                } else if (!m_saveDir.isEmpty()) {
                    QDir base(m_saveDir);
                    dir = base.filePath(isVOC ? "labels_voc/train" : "labels_yolo/train");
                }
            }
            if (!dir.isEmpty()) {
                QDir().mkpath(dir);
                if (ui->leLabels) ui->leLabels->setText(QDir::toNativeSeparators(dir));
                A->setSaveDir(dir);
            }
        };

        auto listImages = [](const QString& dir){
            QStringList out;
            QDir d(dir);
            const QStringList filters = {
                "*.png","*.jpg","*.jpeg","*.bmp","*.tif","*.tiff",
                "*.PNG","*.JPG","*.JPEG","*.BMP","*.TIF","*.TIFF"
            };
            for (const QString& f : d.entryList(filters, QDir::Files, QDir::Name))
                out << d.absoluteFilePath(f);
            return out;
        };

        if (ui->btnAnnotOpen) {
            connect(ui->btnAnnotOpen, &QPushButton::clicked, this, [this, annot, ensureSaveDir]{
                if (!annot) return;
                const QString path = getOpenFileNameSafe(
                    this, tr("Görsel Aç"), QDir::homePath(),
                    tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
                if (path.isEmpty()) return;

                if (ui->cbFormat) annot->setFormat(ui->cbFormat->currentText());
                ensureSaveDir(annot);

                annot->setImageList(QStringList{path}, 0);
                annot->loadImage(path);

                if (ui->lblAnnotInfo) ui->lblAnnotInfo->setText(QFileInfo(path).fileName());

                if (ui->listFiles) {
                    bool prev = ui->listFiles->blockSignals(true);
                    ui->listFiles->clear();
                    auto* it = new QListWidgetItem(QFileInfo(path).fileName());
                    it->setData(Qt::UserRole, path);
                    ui->listFiles->addItem(it);
                    ui->listFiles->setCurrentRow(0);
                    ui->listFiles->blockSignals(prev);
                }
            });
        }

        if (ui->btnAnnotOpenDir) {
            connect(ui->btnAnnotOpenDir, &QPushButton::clicked, this, [this, annot, listImages, ensureSaveDir, loadImagesFromDirLambda]{
                if (!annot) {
                    QMessageBox::warning(this, tr("Annotator yok"), tr("Annotator widget bulunamadı."));
                    return;
                }
                const QString dir = getExistingDirectorySafe(this, tr("Görüntü klasörü seç"), QDir::homePath());
                if (dir.isEmpty()) return;

                const QStringList imgs = listImages(dir);
                if (imgs.isEmpty()) {
                    QMessageBox::information(this, tr("Boş klasör"),
                                             tr("Bu klasörde desteklenen görsel bulunamadı."));
                    return;
                }

                if (ui->leImages)
                    ui->leImages->setText(QDir::toNativeSeparators(dir));

                if (ui->cbFormat) annot->setFormat(ui->cbFormat->currentText());

                QString out;
                if (ui->cbFormat) {
                    const bool isVOC = (ui->cbFormat->currentText()=="PascalVOC");
                    out = QDir(dir).filePath(isVOC ? "labels_voc/train" : "labels_yolo/train");
                    QDir().mkpath(out);
                    annot->setSaveDir(out);
                    if (ui->leLabels) ui->leLabels->setText(QDir::toNativeSeparators(out));
                }
                ensureSaveDir(annot);

                annot->setImageList(imgs, 0);
                annot->loadImage(imgs.first());

                if (ui->lblAnnotInfo) ui->lblAnnotInfo->setText(QFileInfo(imgs.first()).fileName());

                if (ui->listFiles) {
                    loadImagesFromDirLambda(dir);
                }
                if (ui->tabWidget && ui->tab_3) {
                    ui->tabWidget->setCurrentWidget(ui->tab_3);
                }
            });
        }

        if (ui->btnAnnotPrev) {
            connect(ui->btnAnnotPrev, &QPushButton::clicked, this, [annot, this]{
                annot->prevImage();
                if (ui->lblAnnotInfo) ui->lblAnnotInfo->setText(QFileInfo(annot->currentImage()).fileName());
            });
        }

        if (ui->btnAnnotNext) {
            connect(ui->btnAnnotNext, &QPushButton::clicked, this, [annot, this]{
                annot->nextImage();
                if (ui->lblAnnotInfo) ui->lblAnnotInfo->setText(QFileInfo(annot->currentImage()).fileName());
            });
        }

        if (ui->btnAnnotSave) {
            connect(ui->btnAnnotSave, &QPushButton::clicked, this, [annot, this, ensureSaveDir]{
                if (ui->cbFormat) annot->setFormat(ui->cbFormat->currentText());
                ensureSaveDir(annot);
                if (!annot->saveCurrent()) {
                    qWarning() << "Annotator: kaydetme başarısız";
                } else if (statusBar()) {
                    statusBar()->showMessage(tr("Kaydedildi: %1")
                                                 .arg(QFileInfo(annot->currentImage()).completeBaseName()), 2000);
                }
            });
        }
    }
}

// ================================
//  DTOR
// ================================
MainWindow::~MainWindow()
{
    if (m_camera) m_camera->stop();
    if (m_trainProc) { m_trainProc->kill(); m_trainProc->deleteLater(); }
    if (m_inferProc) { m_inferProc->kill(); m_inferProc->deleteLater(); }
    delete ui;
    ui = nullptr;
}

// ================================
//  Resize
// ================================
void MainWindow::resizeEvent(QResizeEvent *e)
{
    if (e) QMainWindow::resizeEvent(e);
    if (!ui->menuFrame || !m_previewBox) return;

    const int margin = 10;
    int reservedTop = 0;
    const auto kids = ui->menuFrame->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* w : kids) {
        if (w == m_previewBox) continue;
        if (!w->isVisible()) continue;
        reservedTop = std::max(reservedTop, w->geometry().bottom());
    }
    reservedTop += margin;

    QRect r = ui->menuFrame->contentsRect();
    r.adjust(margin, reservedTop, -margin, -margin);

    if (r.height() < 100 || r.width() < 120) m_previewBox->hide();
    else {
        m_previewBox->setGeometry(r);
        m_previewBox->show();
    }

    if (QWidget *annotFullHost = this->findChild<QWidget*>("annotFullHost")) {
        if (ui->centralwidget) {
            QRect cr = ui->centralwidget->contentsRect();
            annotFullHost->setGeometry(cr);
        }
    }
}

// ================================
//  Kayıt yolları
// ================================
QString MainWindow::makeSavePath() const
{
    if (!m_saveDir.isEmpty())
        return m_saveDir;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString dir  = base + "/CameraMenuApp";
    QDir().mkpath(dir);
    return dir;
}

QString MainWindow::makeFileName() const
{
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmsszzz");
    return QString("img_%1.jpg").arg(ts);
}

// ================================
//  Çekim
// ================================
void MainWindow::takeOne()
{
    if (!m_imageCap || !m_imageCap->isReadyForCapture()) return;
    if (!ui->cmbLabel || ui->cmbLabel->currentText().trimmed().isEmpty()) return;

    const QString saveDir = classDir();
    QDir().mkpath(saveDir);
    const QString full = saveDir + "/" + makeFileName();
    m_imageCap->captureToFile(full);
}

void MainWindow::burstStart()
{
    if (!m_imageCap) return;
    if (!ui->cmbLabel || ui->cmbLabel->currentText().trimmed().isEmpty()) return;

    m_burstActive = true;
    m_burstTaken  = 0;
    nextBurstShot();
    m_burstTimer.start();
}

void MainWindow::burstStop()
{
    m_burstActive = false;
    m_burstTimer.stop();
}

void MainWindow::nextBurstShot()
{
    if (!m_burstActive) return;
    if (m_burstTaken >= m_burstTargetCount) { burstStop(); return; }
    if (!m_imageCap->isReadyForCapture())   { return; }

    const QString saveDir = classDir();
    QDir().mkpath(saveDir);
    const QString full = saveDir + "/" + makeFileName();

    m_imageCap->captureToFile(full);
    ++m_burstTaken;
}

void MainWindow::chooseDir()
{
    const QString start = m_saveDir.isEmpty()
    ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
    : m_saveDir;

    QString dir = getExistingDirectorySafe(
        this,
        tr("Dataset kökünü seç (içinde class klasörleri olacak)"),
        start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;

    m_saveDir = dir;
    QDir root(m_saveDir);

    root.mkpath("images_to_label");
    root.mkpath("labels_yolo/train");
    root.mkpath("labels_voc/train");

    const QString classesPath = root.filePath("classes.txt");
    if (!QFileInfo::exists(classesPath)) {
        QFile f(classesPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("background\nperson\n");
            f.close();
        }
    }

    const QString imagesPath = root.filePath("images_to_label");
    const bool isVOC = (ui->cbFormat && ui->cbFormat->currentText() == "PascalVOC");
    const QString labelsPath = root.filePath(isVOC ? "labels_voc/train" : "labels_yolo/train");

    if (ui->leImages)  ui->leImages->setText(QDir::toNativeSeparators(imagesPath));
    if (ui->leLabels)  ui->leLabels->setText(QDir::toNativeSeparators(labelsPath));
    if (ui->leClasses) {
        ui->leClasses->setEnabled(true);
        ui->leClasses->setReadOnly(false);
        ui->leClasses->setText(QDir::toNativeSeparators(classesPath));
    }

    if (m_dirLabel)  m_dirLabel->setText("Kayıt klasörü: " + m_saveDir);
    updateLabelCount();
    populateLabelsFromDir();
}

// ================================
//  EĞİTİM
// ================================
void MainWindow::trainStart(){
    if (m_saveDir.isEmpty()) return;

    const QString dataRoot = m_saveDir;
    const QString outDir   = QDir(m_saveDir).filePath("model_out");

    const QString py      = venvPythonPath();
    const QString script  = scriptPath("python/train.py");

    if (!QFileInfo::exists(script) || !QFileInfo::exists(py)) {
        if (ui->txtPredLog) ui->txtPredLog->appendPlainText("Eğitim betiği veya Python yolu bulunamadı.");
        return;
    }

    if (m_trainProc){ m_trainProc->kill(); m_trainProc->deleteLater(); }
    m_trainProc = new QProcess(this);
    m_trainProc->setProgram(py);
    m_trainProc->setArguments({ script, "--data", dataRoot, "--out", outDir, "--epochs", "3" });
    m_trainProc->setProcessChannelMode(QProcess::MergedChannels);
    m_trainProc->setProcessEnvironment(makePythonEnv());
    m_trainProc->setWorkingDirectory(projectRoot());

    connect(m_trainProc, &QProcess::readyRead, this, [this]{
        const QString s = QString::fromUtf8(m_trainProc->readAll());
        if (ui->txtPredLog && !s.trimmed().isEmpty())
            ui->txtPredLog->appendPlainText(s.trimmed());
    });
    connect(m_trainProc, &QProcess::finished, this, [this](int, QProcess::ExitStatus){
        if (ui->txtPredLog) ui->txtPredLog->appendPlainText("Eğitim bitti");
    });

    m_trainProc->start();
}

// ================================
//  MODEL YÜKLE & TAHMİN
// ================================
void MainWindow::loadModel() {
    m_modelPath = QDir::toNativeSeparators(projectRoot() + "/model_out/model_best.pth");
    if (ui->lblPred)
        ui->lblPred->setText("Model: " + QFileInfo(m_modelPath).fileName());
}

void MainWindow::predict() {
    if (m_modelPath.isEmpty()) return;

    if (!m_lastSavedPath.isEmpty() && QFileInfo::exists(m_lastSavedPath)) {
        startInferProcess(m_lastSavedPath);
    } else {
        const QString tmpDir  = makeSavePath();
        QDir().mkpath(tmpDir);
        const QString tmpPath = QDir(tmpDir).filePath("tmp_infer.jpg");

        m_runInferWhenSaved = true;
        if (m_imageCap && m_imageCap->isReadyForCapture())
            m_imageCap->captureToFile(tmpPath);
    }
}

void MainWindow::startInferProcess(const QString& imagePath) {
    if (m_inferProc) { m_inferProc->kill(); m_inferProc->deleteLater(); }
    m_inferProc = new QProcess(this);

    const QString py     = venvPythonPath();
    const QString script = scriptPath("python/infer.py");

    if (!QFileInfo::exists(script) || !QFileInfo::exists(py)) {
        if (ui->txtPredLog) ui->txtPredLog->appendPlainText("Tahmin betiği veya Python yolu bulunamadı.");
        return;
    }

    m_inferProc->setProgram(py);
    m_inferProc->setArguments({
        script,
        "--model", QDir::toNativeSeparators(projectRoot() + "/model_out/model_best.pth"),
        "--image", imagePath
    });
    m_inferProc->setProcessChannelMode(QProcess::MergedChannels);
    m_inferProc->setProcessEnvironment(makePythonEnv());
    m_inferProc->setWorkingDirectory(projectRoot());

    connect(m_inferProc, &QProcess::readyRead, this, [this]{
        const QString s = QString::fromUtf8(m_inferProc->readAll());
        const QString line = s.trimmed();
        if (ui->lblPred && !line.isEmpty())
            ui->lblPred->setText(line);
        if (ui->txtPredLog && !line.isEmpty())
            ui->txtPredLog->appendPlainText(line);
    });

    connect(m_inferProc, &QProcess::finished, this, [this](int, QProcess::ExitStatus){
        if (ui->txtPredLog) ui->txtPredLog->appendPlainText("Tahmin bitti");

        if (m_inferProc) { m_inferProc->deleteLater(); m_inferProc=nullptr; }

        if (!g_predQueue.isEmpty()) {
            const QString nextImg = g_predQueue.takeFirst();
            if (ui->txtPredLog) {
                ui->txtPredLog->appendPlainText(
                    tr("Sıradaki: %1 (kalan %2)").arg(QFileInfo(nextImg).fileName()).arg(g_predQueue.size()));
            }
            startInferProcess(nextImg);
        }
    });

    if (ui->lblPred) ui->lblPred->setText("Tahmin ediliyor…");
    if (ui->txtPredLog) ui->txtPredLog->appendPlainText(tr("Çalışıyor: %1").arg(imagePath));
    m_inferProc->start();
}
