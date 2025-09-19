// mainwindow_labeling.cpp
// Labeling ile ilgili TÜM akış burada toplanmıştır.

#include "mainwindow.h"
#include "annotatorwidget.h"
#include "ui_mainwindow.h"
#include "label_utils.h"        // <<< EKLENDİ

#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDateTime>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSet>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QStandardPaths>
#include <QWidget>          // QWIDGETSIZE_MAX için
#include <QtMath>
#include <QImage>           // <-- EKLENDİ: XYXY/XYWH piksel → normalize
#include <algorithm>        // <-- EKLENDİ: std::max/min

// ======================================================================
// 1) Bizimle Başlat (AUTO-CONNECT)
// ======================================================================
void MainWindow::on_btnStartLabel_clicked()
{
    // Annotator ayarlarını UI'dan al
    if (ui->annotView && ui->cbFormat)
        ui->annotView->setFormat(ui->cbFormat->currentText());
    if (ui->annotView && ui->leClasses && !ui->leClasses->text().isEmpty())
        ui->annotView->loadClassesFromFile(ui->leClasses->text());
    if (ui->annotView && ui->leLabels && !ui->leLabels->text().isEmpty())
        ui->annotView->setSaveDir(ui->leLabels->text());

    const QString p = ui->leImages ? ui->leImages->text().trimmed() : QString();
    bool ok = false;
    if (p.isEmpty()) {
        QMessageBox::warning(this, "Uyarı", "Önce görselleri seçin.");
        return;
    }
    if (QFileInfo(p).isDir()) {
        ok = ui->annotView ? ui->annotView->openDir(p) : false;
    } else {
        ok = ui->annotView ? ui->annotView->openFiles(p.split(';', Qt::SkipEmptyParts)) : false;
    }
    if (!ok) {
        QMessageBox::warning(this, "Açılamadı", "Görseller bulunamadı / açılamadı.");
        return;
    }

    // Full’e geç
    if (ui->annotView) ui->annotView->enterFull();

    if (ui->dockAnnotator) ui->dockAnnotator->show();
    if (ui->tabWidget)     ui->tabWidget->setCurrentIndex(0);   // "Boxes" tab
    if (ui->menuFrame)     ui->menuFrame->hide();
    if (ui->videoHost)     ui->videoHost->hide();

    // “Ben durdurmadan durmasın” → yeniden tıklamayı engelle
    if (ui->btnStartLabel) ui->btnStartLabel->setEnabled(false);

    if (ui->leLabels && ui->lblLabelCount && !ui->leLabels->text().isEmpty()) {
        QDir d(ui->leLabels->text());
        const int n = d.entryList(QStringList() << "*.txt", QDir::Files).size();
        ui->lblLabelCount->setText(QString("Etiket dosyası: %1").arg(n));
    }
}

// ======================================================================
// 2) LabelImg: Yol Seçiciler
// ======================================================================
void MainWindow::chooseImagesDir()
{
    const QString d = getExistingDirectorySafe(
        this,
        tr("Görüntü klasörü seç (images/train veya val)"),
        m_saveDir.isEmpty() ? QDir::homePath() : m_saveDir,
        QFileDialog::Options()
        );
    if (!d.isEmpty() && ui->leImages)
        ui->leImages->setText(QDir::toNativeSeparators(d));
}

void MainWindow::chooseClassesFile()
{
    const QString base = m_saveDir.isEmpty()
    ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
    : m_saveDir;

    QDir root(base);
    const QString clsPath = root.filePath("classes.txt");
    if (!QFileInfo::exists(clsPath)) {
        QFile f(clsPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("background\nperson\n");
            f.close();
        }
    }

    const QString f = getOpenFileNameSafe(
        this,
        tr("Sınıf dosyasını seç"),
        base,
        tr("Class lists (*.txt *.names);;All Files (*.*)"),
        QFileDialog::Options()
        );

    if (!f.isEmpty() && ui->leClasses) {
        ui->leClasses->setEnabled(true);
        ui->leClasses->setReadOnly(false);
        ui->leClasses->setText(QDir::toNativeSeparators(f));
    }
}

void MainWindow::chooseLabelsDir()
{
    const QString d = getExistingDirectorySafe(
        this,
        tr("Kaydetme klasörü seç (labels_yolo/train veya labels_voc/train)"),
        m_saveDir.isEmpty() ? QDir::homePath() : m_saveDir,
        QFileDialog::Options()
        );
    if (!d.isEmpty() && ui->leLabels)
        ui->leLabels->setText(QDir::toNativeSeparators(d));
}

// ======================================================================
// 3) LabelImg Akışı: startLabeling + launchLabelImg
// ======================================================================
// Yardımcı: klasörden görsel listele (case-insensitive) ve mutlak yollar döndür
static QStringList listImagesCI(const QString& dir)
{
    QStringList out;
    QDir d(dir);
    const QStringList files = d.entryList(QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString ext = QFileInfo(f).suffix().toLower();
        if (QStringList{"png","jpg","jpeg","bmp","tif","tiff"}.contains(ext))
            out << QDir(dir).absoluteFilePath(f);
    }
    return out;
}

// Yardımcı: kullanıcı girişi (dosya/klasör/; ile liste) → ilk geçerli görsel
static QString firstImageFromInput(const QString& src)
{
    if (src.contains(';')) {
        const QStringList parts = src.split(';', Qt::SkipEmptyParts);
        for (const QString& p : parts) {
            QFileInfo fi(p.trimmed());
            if (fi.exists() && fi.isFile()) return QDir::toNativeSeparators(fi.absoluteFilePath());
        }
    }
    QFileInfo fi(src);
    if (fi.isFile()) return QDir::toNativeSeparators(fi.absoluteFilePath());
    if (fi.isDir()) {
        const QStringList imgs = listImagesCI(fi.absoluteFilePath());
        if (!imgs.isEmpty()) return QDir::toNativeSeparators(QFileInfo(imgs.first()).absoluteFilePath());
    }
    return {};
}

// Listedeki seçili öğeden mutlak dosya yolu üret
static QString currentFromList(QListWidget* list)
{
    if (!list || !list->currentItem()) return {};
    auto *it = list->currentItem();
    QString p = it->data(Qt::UserRole).toString();
    if (p.isEmpty()) p = it->text();
    if (p.isEmpty()) return {};
    return QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
}

void MainWindow::startLabeling()
{
    // Annotator’da görüntü açıksa onu baz al
    QString openArg;
    if (ui->annotView && !ui->annotView->currentImage().isEmpty())
        openArg = QDir::toNativeSeparators(QFileInfo(ui->annotView->currentImage()).absoluteFilePath());

    // Değilse, leImages'ten üret (dosya ya da klasör → ilk görsel)
    if (openArg.isEmpty()) {
        const QString src = ui->leImages ? ui->leImages->text().trimmed() : QString();
        if (src.isEmpty()) {
            QMessageBox::warning(this, tr("Eksik bilgi"), tr("Görüntü veya klasör seçin."));
            return;
        }
        openArg = firstImageFromInput(src);
    }

    const QString classesTxt = ui->leClasses ? ui->leClasses->text().trimmed() : QString();
    const QString labelsDir  = ui->leLabels  ? ui->leLabels->text().trimmed()  : QString();
    const QString format     = ui->cbFormat  ? ui->cbFormat->currentText()     : QString("YOLO");

    if (openArg.isEmpty() || labelsDir.isEmpty()) {
        QMessageBox::warning(this, tr("Eksik bilgi"),
                             tr("Görsel/klasör ve kaydetme klasörünü seçin."));
        return;
    }

    // *** DÜZENLEME: LabelImg'i DAİMA KLASÖR ile başlat ***
    const QString imagesDir = QFileInfo(openArg).absolutePath();
    if (auto le = findChild<QLineEdit*>("leImages")) {
        if (QDir::cleanPath(le->text()) != QDir::cleanPath(imagesDir)) le->setText(imagesDir);
    }

    QDir().mkpath(labelsDir);
    if (ui->txtPredLog) ui->txtPredLog->appendPlainText("[startLabeling] file = " + openArg);

    // Klasörle başlat (1/0 görünmesini önler)
    launchLabelImg(imagesDir, classesTxt, format, labelsDir);
}

void MainWindow::launchLabelImg(const QString& imagesDir,
                                const QString& classesPath,
                                const QString& /*format*/,
                                const QString& saveDir)
{
    const QString root    = projectRoot(); // sınıf üyesi
    const QString repoDir = QDir::toNativeSeparators(root + "/third_party/labelImg");
    const QString entryPy = QDir(repoDir).filePath("labelImg.py");

#ifdef Q_OS_WIN
    // <<< DEĞİŞTİ: pythonw yerine python.exe
    QString py = QDir::toNativeSeparators(root + "/venv/Scripts/python.exe");
    const QString pyrcc = QDir::toNativeSeparators(root + "/venv/Scripts/pyrcc5.exe");
#else
    QString py = venvPythonPath(); // sınıf üyesi (genelde "python3")
    const QString pyrcc = "pyrcc5";
#endif

    auto existsStr = [](const QString& p)->QString { return QFileInfo::exists(p) ? QStringLiteral("OK") : QStringLiteral("YOK"); };

    // resources.py yoksa üret (sessiz best-effort)
    const QString resPy  = QDir(repoDir).filePath("libs/resources.py");
    const QString resQrc = QDir(repoDir).filePath("resources.qrc");
    if (!QFileInfo::exists(resPy) && QFileInfo::exists(resQrc) && QFileInfo::exists(pyrcc)) {
        QProcess::execute(pyrcc, QStringList() << "-o" << resPy << resQrc);
    }

    // Argümanları mutlak yola çevir + logla
    QString imagesAbs  = imagesDir.isEmpty()   ? QString() : QDir::toNativeSeparators(QFileInfo(imagesDir).absoluteFilePath());
    QString classesAbs = classesPath.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(classesPath).absoluteFilePath());
    QString saveAbs    = saveDir.isEmpty()     ? QString() : QDir::toNativeSeparators(QFileInfo(saveDir).absoluteFilePath());

    QStringList args;
    args << entryPy;
    if (!imagesAbs.isEmpty())  args << imagesAbs;   // klasör veya dosya yolu gelebilir
    if (!classesAbs.isEmpty()) args << classesAbs;
    if (!saveAbs.isEmpty())    args << saveAbs;

    if (ui->txtPredLog) {
        // Anahtarlar net: imagesDir / classes / saveDir
        ui->txtPredLog->appendPlainText("[launchLabelImg] imagesDir = " + (imagesAbs.isEmpty() ? "<empty>" : imagesAbs));
        ui->txtPredLog->appendPlainText("[launchLabelImg] classes   = " + (classesAbs.isEmpty() ? "<empty>" : classesAbs));
        ui->txtPredLog->appendPlainText("[launchLabelImg] saveDir   = " + (saveAbs.isEmpty() ? "<empty>" : saveAbs));
    }

    const bool okPaths =
        QFileInfo::exists(py) &&
        QFileInfo::exists(entryPy) &&
        QDir(repoDir).exists();

    // Denenecek tam komutu metin halinde derle (teşhis için)
    const QString cmdText =
        QString("WD=%1\n%2 %3")
            .arg(repoDir,
                 QDir::toNativeSeparators(py),
                 args.join(' '));

    if (!okPaths) {
        QMessageBox::warning(this, tr("Eksik Yol"),
                             tr("Gerekli yollardan biri yok:\n\n"
                                "projectRoot: %1\n"
                                "repoDir:     %2  [%3]\n"
                                "labelImg.py: %4  [%5]\n"
                                "python(.w):  %6  [%7]\n\n"
                                "Komut:\n%8")
                                 .arg(root,
                                      repoDir,  existsStr(repoDir),
                                      entryPy,  existsStr(entryPy),
                                      py,       existsStr(py),
                                      cmdText));
        return;
    }

    // --- Non-detached QProcess: logları UI'ye akıt ---
    QProcess* p = new QProcess(this);
    p->setProgram(py);
    p->setArguments(args);
    p->setWorkingDirectory(repoDir);
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->setProcessEnvironment(makePythonEnv());  // sınıf üyesi

    // Log akışı
    connect(p, &QProcess::readyRead, this, [this, p]{
        const QString s = QString::fromUtf8(p->readAll()).trimmed();
        if (!s.isEmpty() && ui && ui->txtPredLog) ui->txtPredLog->appendPlainText("[labelImg] " + s);
    });
    // Hata/son durum
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError){
        if (ui && ui->txtPredLog) ui->txtPredLog->appendPlainText(
                QString("[labelImg] error: %1").arg(p->errorString()));
        QMessageBox::warning(this, tr("Başlatılamadı"),
                             tr("LabelImg açılamadı.\nHata: %1").arg(p->errorString()));
    });
    connect(p, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st){
                if (ui && ui->txtPredLog) ui->txtPredLog->appendPlainText(
                        QString("[labelImg] exited: code=%1 status=%2").arg(code).arg(int(st)));
            });

    // Başlat
    p->start();

    // İsteğe bağlı: 3 sn içinde başlatamadıysa kullanıcıyı bilgilendir
    QTimer::singleShot(3000, this, [this, p]{
        if (p->state() != QProcess::Running) {
            if (ui && ui->txtPredLog) ui->txtPredLog->appendPlainText(
                    "[labelImg] 3sn içinde RUNNING olmadı.");
        }
    });

    if (statusBar())
        statusBar()->showMessage(tr("LabelImg başlatılıyor (repo)..."), 2000);
}

// ======================================================================
// 4) Etiket Dosyası Sayacı
// ======================================================================
int MainWindow::countLabelFiles(const QString& dirPath, const QStringList& exts) const
{
    QDir d(dirPath); int total = 0;
    for (const auto& e : exts)
        total += d.entryList({ "*."+e }, QDir::Files).size();
    return total;
}

void MainWindow::updateLabelCount()
{
    if (!ui->lblLabelCount) return;
    const QString out = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
    if (out.isEmpty()) { ui->lblLabelCount->setText("Etiket dosyası: 0"); return; }

    const bool isVOC = (ui->cbFormat && ui->cbFormat->currentText() == "PascalVOC");
    const int n = countLabelFiles(out, isVOC ? QStringList{"xml"} : QStringList{"txt"});
    ui->lblLabelCount->setText(QString("Etiket dosyası: %1").arg(n));
}

// ======================================================================
// 5) Dock annotator tam ekran yardımcıları
// ======================================================================
void MainWindow::on_dockAnnotator_destroyed()
{
    // boş
}

void MainWindow::enterAnnotatorFull()
{
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
}

void MainWindow::exitAnnotatorFull()
{
    if (ui->annotView) ui->annotView->exitFull();
}

// Opsiyonel: ayrı pencerede sadece labellama
void MainWindow::openAnnotatorInNewWindow()
{
    QWidget *w = new QWidget;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setWindowTitle(tr("Labellama Penceresi"));
    w->resize(900, 650);

    auto *root = new QVBoxLayout(w);
    root->setContentsMargins(10,10,10,10);
    root->setSpacing(8);

    auto *bar = new QWidget(w);
    auto *hb  = new QHBoxLayout(bar);
    hb->setContentsMargins(0,0,0,0);
    hb->setSpacing(6);

    QPushButton *bOpen   = new QPushButton(tr("Görsel Aç"), bar);
    QPushButton *bOpenD  = new QPushButton(tr("Klasör Aç"), bar);
    QPushButton *bPrev   = new QPushButton(tr("← Önceki"), bar);
    QPushButton *bNext   = new QPushButton(tr("Sonraki →"), bar);
    QPushButton *bSave   = new QPushButton(tr("Kaydet"), bar);
    QPushButton *bBack   = new QPushButton(tr("← Geri Dön"), bar);
    QLabel      *infoLbl = new QLabel(tr("Hazır"), bar);

    hb->addWidget(bOpen);
    hb->addWidget(bOpenD);
    hb->addSpacing(10);
    hb->addWidget(bPrev);
    hb->addWidget(bNext);
    hb->addSpacing(10);
    hb->addWidget(bSave);
    hb->addStretch(1);
    hb->addWidget(infoLbl);
    hb->addSpacing(10);
    hb->addWidget(bBack);

    AnnotatorWidget *A = new AnnotatorWidget(w);

    root->addWidget(bar);
    root->addWidget(A, 1);

    const QString fmt = ui->cbFormat ? ui->cbFormat->currentText() : QString("YOLO");
    A->setFormat(fmt);

    QString activeClass;
    if (ui->chkUseDefault && ui->chkUseDefault->isChecked() && ui->txtDefaultClass)
        activeClass = ui->txtDefaultClass->text().trimmed();
    else if (ui->cmbClass)
        activeClass = ui->cmbClass->currentText().trimmed();
    if (!activeClass.isEmpty())
        A->setActiveClass(activeClass);

    QString outDir = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
    if (outDir.isEmpty()) {
        const bool isVOC = (fmt == "PascalVOC");
        if (!m_saveDir.isEmpty()) {
            QDir base(m_saveDir);
            outDir = base.filePath(isVOC ? "labels_voc/train" : "labels_yolo/train");
        }
    }
    if (!outDir.isEmpty()) {
        QDir().mkpath(outDir);
        A->setSaveDir(outDir);
    }

    if (ui->listFiles && ui->listFiles->count() > 0) {
        QStringList imgs;
        imgs.reserve(ui->listFiles->count());
        for (int i=0;i<ui->listFiles->count();++i) {
            auto *it = ui->listFiles->item(i);
            QString p = it->data(Qt::UserRole).toString();
            if (p.isEmpty()) p = it->text();
            if (!p.isEmpty()) imgs << QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
        }
        if (!imgs.isEmpty()) {
            A->setImageList(imgs, qMax(0, ui->listFiles->currentRow()));
            A->loadImage(imgs.value(qMax(0, ui->listFiles->currentRow())));
            infoLbl->setText(QFileInfo(A->currentImage()).fileName());
        }
    } else if (ui->annotView && !ui->annotView->currentImage().isEmpty()) {
        const QString cur = ui->annotView->currentImage();
        A->setImageList(QStringList{cur}, 0);
        A->loadImage(cur);
        infoLbl->setText(QFileInfo(cur).fileName());
    }

    auto ensureSaveDir = [&](AnnotatorWidget* W, const QString& srcImgDir){
        if (!W) return;
        QString dir = outDir;
        const bool isVOC = (fmt == "PascalVOC");
        if (dir.isEmpty() && !srcImgDir.isEmpty()) {
            QDir base(srcImgDir);
            dir = base.filePath(isVOC ? "labels_voc/train" : "labels_yolo/train");
        }
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            W->setSaveDir(dir);
        }
    };

    QObject::connect(bOpen, &QPushButton::clicked, w, [=]{
        const QString path = getOpenFileNameSafe(
            w, tr("Görsel Aç"), QDir::homePath(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"),
            QFileDialog::Options()
            );
        if (path.isEmpty()) return;
        A->setImageList(QStringList{path}, 0);
        A->loadImage(path);
        ensureSaveDir(A, QFileInfo(path).absolutePath());
        infoLbl->setText(QFileInfo(path).fileName());
    });

    QObject::connect(bOpenD, &QPushButton::clicked, w, [=]{
        const QString dir = getExistingDirectorySafe(
            w, tr("Görüntü klasörü seç"), QDir::homePath(), QFileDialog::Options()
            );
        if (dir.isEmpty()) return;
        const QStringList imgs = listImagesCI(dir);
        if (imgs.isEmpty()) return;
        A->setImageList(imgs, 0);
        A->loadImage(imgs.first());
        ensureSaveDir(A, dir);
        infoLbl->setText(QFileInfo(imgs.first()).fileName());
    });

    QObject::connect(bPrev, &QPushButton::clicked, w, [=]{
        A->prevImage();
        infoLbl->setText(QFileInfo(A->currentImage()).fileName());
    });
    QObject::connect(bNext, &QPushButton::clicked, w, [=]{
        A->nextImage();
        infoLbl->setText(QFileInfo(A->currentImage()).fileName());
    });
    QObject::connect(bSave, &QPushButton::clicked, w, [=]{
        if (!A->saveCurrent())
            QMessageBox::warning(w, tr("Kayıt"), tr("Kaydedilemedi."));
        else
            infoLbl->setText(tr("Kaydedildi: %1").arg(QFileInfo(A->currentImage()).completeBaseName()));
    });
    QObject::connect(bBack, &QPushButton::clicked, w, [w]{ w->close(); });

    w->show();
}

// ======================================================================
// 6) Video genişlik kilidi utils
// ======================================================================
void MainWindow::lockVideoWidth(int w)
{
    m_lockedVideoW = qMax(0, w);
    if (ui && ui->videoHost) {
        ui->videoHost->setMinimumWidth(m_lockedVideoW);
        ui->videoHost->setMaximumWidth(m_lockedVideoW);
    }
}

void MainWindow::unlockVideoWidth()
{
    m_lockedVideoW = -1;
    if (ui && ui->videoHost) {
        ui->videoHost->setMinimumWidth(0);
        ui->videoHost->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

int MainWindow::currentVideoWidth() const
{
    if (ui && ui->videoHost) return ui->videoHost->width();
    return -1;
}

// ======================================================================
// 7) Annotator log slotları
// ======================================================================
void MainWindow::onAnnotatorInfo(const QString& msg)
{
    if (ui && ui->txtLog) ui->txtLog->appendPlainText(msg);
}

void MainWindow::onAnnotatorLog(const QString& line)
{
    if (ui && ui->txtLog) ui->txtLog->appendPlainText(line);
}

// ======================================================================
// 8) LabelImg Butonu (Handler) — sade/temiz (label_utils ile)
// ======================================================================
void MainWindow::on_btnStartLabelImg_clicked()
{
    // --- EKLENDİ: leLabels bir .txt'yi gösteriyorsa, aynı stem’li görüntüyü bul ---
    auto findImageByStem = [&](const QString& labelTxt)->QString {
        QFileInfo lfi(labelTxt);
        if (!lfi.exists() || lfi.suffix().toLower() != "txt") return {};

        const QString stem = lfi.completeBaseName(); // "img_2025...."
        // labels_* klasörünün iki üstüne çık: .../(dataset/person)
        QDir root = lfi.dir();       // .../labels_yolo[_li]/train
        root.cdUp();                 // .../labels_yolo[_li]
        root.cdUp();                 // .../(dataset/person)
        if (!root.exists()) return {};

        const QStringList imgRoots = {
            "images/train","images/val",
            "images_yolo/train","images_yolo/val",
            "images_li/train","images_li/val",
            "images_labelimg/train","images_labelimg/val",
            "images_voc/train","images_voc/val"
        };
        const QStringList exts = {"png","jpg","jpeg","bmp","tif","tiff"};

        for (const QString& sub : imgRoots) {
            QDir d(root.filePath(sub));
            if (!d.exists()) continue;
            for (const QString& e : exts) {
                const QString cand = d.absoluteFilePath(stem + "." + e);
                if (QFileInfo::exists(cand)) return QDir::toNativeSeparators(cand);
            }
        }
        return {};
    };
    // --- EKLENDİ BİTTİ ---

    QString openArg;

    // 0) leLabels .txt ise: aynı stem'li görüntüyü DOĞRUDAN tespit et
    if (openArg.isEmpty()) {
        const QString lbl = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
        if (!lbl.isEmpty() && lbl.endsWith(".txt", Qt::CaseInsensitive)) {
            const QString imgFromTxt = findImageByStem(lbl);
            if (!imgFromTxt.isEmpty())
                openArg = imgFromTxt; // aynı kare
        }
    }

    // 1) Dock annotator'da açık görsel varsa onu kullan
    if (openArg.isEmpty() && ui->annotView && !ui->annotView->currentImage().isEmpty()) {
        openArg = QDir::toNativeSeparators(QFileInfo(ui->annotView->currentImage()).absoluteFilePath());
    }

    // 2) Yoksa Files listesindeki SEÇİLİ öğeyi dene
    if (openArg.isEmpty() && ui->listFiles) {
        openArg = currentFromList(ui->listFiles);
    }

    // 3) Hâlâ boşsa, leImages (dosya/klasör) → ilk geçerli görsel
    if (openArg.isEmpty()) {
        const QString src = ui->leImages ? ui->leImages->text().trimmed() : QString();
        if (src.isEmpty()) {
            QMessageBox::warning(this, tr("Uyarı"), tr("Önce görsel veya klasör seçin."));
            return;
        }
        openArg = firstImageFromInput(src);
    }

    if (openArg.isEmpty()) {
        QMessageBox::warning(this, tr("Uyarı"), tr("Açılacak uygun görsel bulunamadı."));
        return;
    }

    // === label_utils ile yol hazırlama + 3 log ===
    const QString fmt      = ui->cbFormat ? ui->cbFormat->currentText() : QString("YOLO");
    const QString baseRoot = "C:/Users/DARK/Documents/CameraMenuApp/dataset/person";
    const QString openFileAbs = lu::absNative(openArg);

    lu::LaunchTriplet P = lu::preparePaths(
        openFileAbs,
        ui->leClasses ? ui->leClasses->text().trimmed() : QString(),
        ui->leLabels  ? ui->leLabels ->text().trimmed() : QString(),
        fmt,
        baseRoot,
        /*separateLI=*/true   // karşılaştırma için ayrık klasör (labels_*_li/train)
        );

    // UI senkronu (leImages sadece klasörle güncellenir)
    if (auto le = findChild<QLineEdit*>("leImages")) {
        if (QDir::cleanPath(le->text()) != QDir::cleanPath(P.imagesDir))
            le->setText(P.imagesDir);
    }
    if (ui->leLabels && QDir::cleanPath(ui->leLabels->text()) != QDir::cleanPath(P.saveDir))
        ui->leLabels->setText(P.saveDir);

    // Klasörde en az 2 görsel var mı? (uyarı—akışı kesmek istemiyorsan return koyma)
    if (!lu::hasAtLeastNImages(P.imagesDir, 2)) {
        QMessageBox::warning(this, "LabelImg",
                             "Bu klasörde geçiş yapacak kadar görsel yok.\n\nKlasör: " + P.imagesDir);
        // return; // istersen burada kesebilirsin
    }

    // --- Teşhis logları ---
    lu::logLaunch3(ui->txtPredLog, P); // imagesDir/classes/saveDir
    if (auto log = findChild<QPlainTextEdit*>("txtPredLog")) {
        log->appendPlainText(QString("[launchLabelImg] current   = ") + openFileAbs);
        // <<< YENİ: classes & saveDir'i current ile birlikte ayrıca yaz
        log->appendPlainText(QString("[launchLabelImg] classes   = ") + P.classesPath);
        log->appendPlainText(QString("[launchLabelImg] saveDir   = ") + P.saveDir);
    }

    // *** DÜZENLEME: LabelImg’i DOSYAYLA başlat ***
    launchLabelImg(openFileAbs, P.classesPath, fmt, P.saveDir);
}

// ======================================================================
// 9) (GÜNCEL) YOLO ↔ XYXY/XYWH/VOC karşılaştırma (IoU)
// ======================================================================

// --- Biçim tespit/okuma yardımcıları ---
enum class AnnFmt { YOLO, XYXY_NORM, XYXY_PIX, XYWH_NORM, XYWH_PIX, VOC_XML, UNKNOWN };

static inline bool looksNormalized(double v) { return v >= 0.0 && v <= 1.0; }

static AnnFmt detectFormatFromLine(const QString& line)
{
    const QString ln = line.trimmed();
    if (ln.isEmpty()) return AnnFmt::UNKNOWN;
    const QStringList t = ln.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (t.isEmpty()) return AnnFmt::UNKNOWN;

    // YOLO: 5+ token ve ilk token integer sınıf
    bool okc=false; t[0].toInt(&okc);
    if (okc && t.size() >= 5) return AnnFmt::YOLO;

    // XY* varyantları: en az 4 sayı (sınıf başta/sonda olabilir)
    int numCnt = 0;
    for (const auto& s : t) { bool ok=false; s.toDouble(&ok); if (ok) ++numCnt; }
    if (numCnt >= 4) {
        double a=t[0].toDouble(), b=t[1].toDouble(), c=t[2].toDouble(), d=t[3].toDouble();
        const bool allNorm = looksNormalized(a)&&looksNormalized(b)&&looksNormalized(c)&&looksNormalized(d);
        if (c > a && d > b) return allNorm ? AnnFmt::XYXY_NORM : AnnFmt::XYXY_PIX; // x2>x1, y2>y1
        return allNorm ? AnnFmt::XYWH_NORM : AnnFmt::XYWH_PIX;                     // x,y,w,h
    }
    return AnnFmt::UNKNOWN;
}

// Görsel kök klasörü (leImages dosya ise klasörü al)
static QString normalizeImagesRoot(const QString& leImagesText)
{
    if (leImagesText.isEmpty()) return {};
    QFileInfo fi(leImagesText);
    if (fi.isFile()) return fi.absolutePath();
    return fi.absoluteFilePath();
}

static QSize findImageSizeForStem(const QString& stem, const QString& imagesRootDir)
{
    if (imagesRootDir.isEmpty()) return {};
    QDir d(imagesRootDir);
    if (!d.exists()) return {};
    const QStringList exts = {"png","jpg","jpeg","bmp","tif","tiff"};
    for (const auto& e : exts) {
        const QString p = d.absoluteFilePath(stem + "." + e);
        if (QFileInfo::exists(p)) {
            QImage img(p);
            if (!img.isNull()) return img.size();
        }
    }
    return {};
}

struct XYXY { int cls; double x1,y1,x2,y2; };

// VOC XML → XYXY (0..1)
static QVector<XYXY> readVOCxmlNorm(const QString& xmlPath)
{
    QVector<XYXY> out;
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly|QIODevice::Text)) return out;
    const QString xml = QString::fromUtf8(f.readAll());
    f.close();

    auto getTag = [&](const QString& tag)->QString{
        QRegularExpression rx(QString("<%1>([^<]+)</%1>").arg(tag));
        auto m = rx.match(xml);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };
    const int W = getTag("width").toInt();
    const int H = getTag("height").toInt();
    if (W<=0 || H<=0) return out;

    QRegularExpression rxObj("<object>([\\s\\S]*?)</object>");
    auto it = rxObj.globalMatch(xml);
    while (it.hasNext()) {
        const QString seg = it.next().captured(1);
        auto val = [&](const char* tag)->int{
            QRegularExpression rr(QString("<%1>([^<]+)</%1>").arg(tag));
            auto mm = rr.match(seg);
            return mm.hasMatch() ? mm.captured(1).toInt() : 0;
        };
        const int xmin = val("xmin"), ymin = val("ymin"), xmax = val("xmax"), ymax = val("ymax");
        if (xmax>xmin && ymax>ymin) {
            XYXY b;
            b.cls = 0; // sınıf haritası yoksa 0
            b.x1 = double(xmin)/W; b.y1 = double(ymin)/H;
            b.x2 = double(xmax)/W; b.y2 = double(ymax)/H;
            out.push_back(b);
        }
    }
    return out;
}

// TXT → XYXY (0..1): YOLO / XYXY / XYWH (norm veya piksel)
static QVector<XYXY> readGenericNorm(const QString& annPath,
                                     AnnFmt fmt,
                                     const QSize& imgSzIfNeeded)
{
    QVector<XYXY> out;
    if (fmt == AnnFmt::VOC_XML) return readVOCxmlNorm(annPath);

    QFile f(annPath);
    if (!f.open(QIODevice::ReadOnly|QIODevice::Text)) return out;
    QTextStream ts(&f);

    auto pushXYXY = [&](int cls, double x1,double y1,double x2,double y2){
        XYXY b{cls,
               std::max(0.0, std::min(1.0, x1)),
               std::max(0.0, std::min(1.0, y1)),
               std::max(0.0, std::min(1.0, x2)),
               std::max(0.0, std::min(1.0, y2))};
        if (b.x2>b.x1 && b.y2>b.y1) out.push_back(b);
    };

    while (!ts.atEnd()) {
        const QString ln = ts.readLine().trimmed();
        if (ln.isEmpty()) continue;
        const QStringList t = ln.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (t.isEmpty()) continue;

        if (fmt == AnnFmt::YOLO) {
            if (t.size() < 5) continue;
            int cls = t[0].toInt();
            double cx=t[1].toDouble(), cy=t[2].toDouble(), w=t[3].toDouble(), h=t[4].toDouble();
            double x1=cx-w/2.0, y1=cy-h/2.0, x2=cx+w/2.0, y2=cy+h/2.0;
            pushXYXY(cls,x1,y1,x2,y2);
        } else if (fmt == AnnFmt::XYXY_NORM || fmt == AnnFmt::XYXY_PIX) {
            if (t.size() < 4) continue;

            // sınıf nerede? [cls x1 y1 x2 y2] ya da [x1 y1 x2 y2 cls]
            int cls = 0;
            double x1,y1,x2,y2;
            bool firstIsInt=false; t[0].toInt(&firstIsInt);
            if (t.size()>=5 && firstIsInt) {
                cls = t[0].toInt();
                x1  = t[1].toDouble(); y1 = t[2].toDouble();
                x2  = t[3].toDouble(); y2 = t[4].toDouble();
            } else {
                x1  = t[0].toDouble(); y1 = t[1].toDouble();
                x2  = t[2].toDouble(); y2 = t[3].toDouble();
                if (t.size()>=5) cls = t[4].toInt();
            }

            if (fmt == AnnFmt::XYXY_PIX) {
                if (imgSzIfNeeded.isEmpty()) continue;
                x1/=imgSzIfNeeded.width();  x2/=imgSzIfNeeded.width();
                y1/=imgSzIfNeeded.height(); y2/=imgSzIfNeeded.height();
            }
            pushXYXY(cls,x1,y1,x2,y2);
        } else if (fmt == AnnFmt::XYWH_NORM || fmt == AnnFmt::XYWH_PIX) {
            if (t.size() < 4) continue;

            // XYWH için de [cls x y w h] ya da [x y w h cls] ihtimali
            int cls = 0;
            double x,y,w,h;
            bool firstIsInt = false;
            t[0].toInt(&firstIsInt);   // <<< DÜZELTME: sadece &firstIsInt kullan

            if (t.size()>=5 && firstIsInt) {
                cls = t[0].toInt();
                x   = t[1].toDouble(); y = t[2].toDouble();
                w   = t[3].toDouble(); h = t[4].toDouble();
            } else {
                x   = t[0].toDouble(); y = t[1].toDouble();
                w   = t[2].toDouble(); h = t[3].toDouble();
                if (t.size()>=5) cls = t[4].toInt();
            }

            if (fmt == AnnFmt::XYWH_PIX) {
                if (imgSzIfNeeded.isEmpty()) continue;
                x/=imgSzIfNeeded.width();  w/=imgSzIfNeeded.width();
                y/=imgSzIfNeeded.height(); h/=imgSzIfNeeded.height();
            }
            // XYWH'yi sol-üst köşe kabul edip XYXY üret
            double x1=x, y1=y, x2=x+w, y2=y+h;
            pushXYXY(cls,x1,y1,x2,y2);
        }
    }
    return out;
}

// IoU (XYXY norm)
static double iouXYXY(const XYXY& a, const XYXY& b)
{
    const double ix1 = std::max(a.x1,b.x1), iy1 = std::max(a.y1,b.y1);
    const double ix2 = std::min(a.x2,b.x2), iy2 = std::min(a.y2,b.y2);
    const double iw = std::max(0.0, ix2-ix1), ih = std::max(0.0, iy2-iy1);
    const double inter = iw*ih;
    const double areaA = std::max(0.0, a.x2-a.x1) * std::max(0.0, a.y2-a.y1);
    const double areaB = std::max(0.0, b.x2-b.x1) * std::max(0.0, b.y2-b.y1);
    const double uni = areaA + areaB - inter;
    return uni>0 ? inter/uni : 0.0;
}

// ---- TEK-GÖRSEL Karşılaştırma (Annotator'da açık olan) ----
void MainWindow::compareCurrentImageAgainstLabelImg()
{
    // 1) Geçerli görselin tam yolu ve stem
    if (!ui->annotView || ui->annotView->currentImage().isEmpty()) {
        QMessageBox::warning(this, tr("Karşılaştırma"), tr("Önce Annotator'da bir görsel açın."));
        return;
    }
    const QString imgPath = QDir::toNativeSeparators(ui->annotView->currentImage());
    const QString stem    = QFileInfo(imgPath).completeBaseName();

    // 2) Bizim labels klasörü (leLabels)
    QString oursDir = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
    if (oursDir.isEmpty() || !QDir(oursDir).exists()) {
        QMessageBox::warning(this, tr("Karşılaştırma"), tr("Kendi etiket klasörü (leLabels) ayarlı değil."));
        return;
    }

    // 3) LabelImg klasörü: ours/other'ı batch'teki gibi akıllı ayır  <<< DEĞİŞTİRİLDİ
    QString otherDir;
    if (oursDir.contains("_li", Qt::CaseInsensitive) ||
        oursDir.contains("_labelimg", Qt::CaseInsensitive)) {
        // leLabels zaten LabelImg klasörünü gösteriyor → 'ours'u türet, 'other' = leLabels
        otherDir = oursDir;                       // karşı (LabelImg) bu
        QDir od(otherDir);                        // .../labels_yolo_li/train
        od.cdUp();                                // .../labels_yolo_li
        QString parent = od.dirName();            // labels_yolo_li
        QRegularExpression rx("_li(_labelimg)?", QRegularExpression::CaseInsensitiveOption);
        parent.replace(rx, "");                   // labels_yolo
        od.cdUp();                                // .../(dataset/person)
        oursDir = QDir(od.absolutePath()).filePath(parent + "/train");  // .../labels_yolo/train
    } else {
        // leLabels bizim klasörü gösteriyor → diğerini otomatik bul
        otherDir = autoFindOtherLabels(oursDir);
    }

    // Fallback: kullanıcıya sor
    if (otherDir.isEmpty() || !QDir(otherDir).exists()) {
        otherDir = getExistingDirectorySafe(this, tr("LabelImg etiket klasörünü seç"),
                                            QFileInfo(oursDir).absolutePath());
        if (otherDir.isEmpty()) return;
    }

    // Teşhis: dosyaları okumadan önce gör
    if (ui->txtLog) {
        ui->txtLog->appendPlainText(QString("[single] img      = %1").arg(QFileInfo(imgPath).fileName()));
        ui->txtLog->appendPlainText(QString("[single] oursDir  = %1").arg(oursDir));
        ui->txtLog->appendPlainText(QString("[single] otherDir = %1").arg(otherDir));
    }

    // 4) Dosya yolları (hem .txt hem .xml ihtimali)
    const QString paTxt = QDir(oursDir).absoluteFilePath(stem + ".txt");
    const QString paXml = QDir(oursDir).absoluteFilePath(stem + ".xml");
    const QString pbTxt = QDir(otherDir).absoluteFilePath(stem + ".txt");
    const QString pbXml = QDir(otherDir).absoluteFilePath(stem + ".xml");

    QString pa, pb;
    if (QFile::exists(paTxt)) pa = paTxt; else if (QFile::exists(paXml)) pa = paXml;
    if (QFile::exists(pbTxt)) pb = pbTxt; else if (QFile::exists(pbXml)) pb = pbXml;

    // Ek teşhis: seçilen yollar
    if (ui->txtLog) {
        ui->txtLog->appendPlainText(QString("[single] pa=%1 pb=%2")
                                        .arg(pa.isEmpty()? "-" : pa, pb.isEmpty()? "-" : pb));
    }

    if (pa.isEmpty() && pb.isEmpty()) {
        QMessageBox::information(this, tr("Karşılaştırma"),
                                 tr("Bu görsel için her iki tarafta da etiket bulunamadı:\n%1").arg(stem));
        return;
    }
    if (pa.isEmpty() || pb.isEmpty()) {
        QMessageBox::information(this, tr("Karşılaştırma"),
                                 tr("Bu görsel için tek tarafta etiket var.\nBizim: %1\nLabelImg: %2")
                                     .arg(pa.isEmpty() ? "-" : QFileInfo(pa).fileName())
                                     .arg(pb.isEmpty() ? "-" : QFileInfo(pb).fileName()));
        return;
    }

    // 5) Biçimleri algıla + pikselse görsel boyutu
    AnnFmt fmtA = pa.endsWith(".xml", Qt::CaseInsensitive) ? AnnFmt::VOC_XML : AnnFmt::YOLO;
    AnnFmt fmtB = AnnFmt::UNKNOWN;
    if (pb.endsWith(".xml", Qt::CaseInsensitive)) {
        fmtB = AnnFmt::VOC_XML;
    } else {
        QFile fb(pb);
        if (fb.open(QIODevice::ReadOnly|QIODevice::Text)) {
            QTextStream ts(&fb);
            QString firstLine;
            while (!ts.atEnd() && firstLine.isEmpty()) {
                const QString ln = ts.readLine().trimmed();
                if (!ln.isEmpty()) firstLine = ln;
            }
            fb.close();
            fmtB = detectFormatFromLine(firstLine);
        }
    }

    QSize imgSz;
    // Eğer karşı taraf piksel formatıysa, doğrudan açık görselin boyutunu kullan
    if (fmtB == AnnFmt::XYXY_PIX || fmtB == AnnFmt::XYWH_PIX) {
        QImage img(imgPath);
        if (!img.isNull()) imgSz = img.size();
    }

    // 6) Kutuları oku (normalize XYXY)
    QVector<XYXY> A = readGenericNorm(pa, fmtA, {});        // bizim
    QVector<XYXY> B = readGenericNorm(pb, fmtB, imgSz);     // labelimg

    // 7) Greedy eşleştirme (IoU)
    QDoubleSpinBox* sb = this->findChild<QDoubleSpinBox*>("sbIou");
    const double thr = (sb && sb->value()>0.0) ? sb->value() : 0.90;

    QVector<bool> usedB(B.size(), false);
    int TP=0, MIS=0, FP=0, FN=0;
    QStringList detail;

    for (int i=0;i<A.size();++i) {
        int best=-1; double bestIoU=-1.0;
        for (int j=0;j<B.size();++j) {
            if (usedB[j]) continue;
            double d = iouXYXY(A[i], B[j]);
            if (d > bestIoU) { bestIoU = d; best = j; }
        }
        if (best>=0 && bestIoU>=thr) {
            usedB[best] = true;
            if (A[i].cls == B[best].cls) { TP++; detail << QString("✓ match  iou=%1").arg(QString::number(bestIoU, 'f', 3)); }
            else { MIS++; detail << QString("∆ class-mismatch iou=%1").arg(QString::number(bestIoU, 'f', 3)); }
        } else { FP++; detail << "× only-ours"; }
    }
    for (int j=0;j<B.size();++j) if (!usedB[j]) { FN++; detail << "× only-labelimg"; }

    const int denom = TP + MIS + FP + FN;
    const double sim = (denom>0) ? double(TP)/double(denom) : 1.0;

    // 8) Çıktı
    if (ui->txtLog) {
        ui->txtLog->appendPlainText("=== Tek-Görsel Karşılaştırma ===");
        ui->txtLog->appendPlainText("Image : " + QFileInfo(imgPath).fileName());
        ui->txtLog->appendPlainText("Ours  : " + (pa.isEmpty() ? "-" : QFileInfo(pa).fileName()));
        ui->txtLog->appendPlainText("LabelImg: " + (pb.isEmpty() ? "-" : QFileInfo(pb).fileName()));
        ui->txtLog->appendPlainText(QString("TP=%1  MIS=%2  FP=%3  FN=%4  IoU>=%5  Similarity=%6%%")
                                        .arg(TP).arg(MIS).arg(FP).arg(FN)
                                        .arg(QString::number(thr, 'f', 2))
                                        .arg(QString::number(sim*100.0, 'f', 1)));
        for (const auto& ln : detail) ui->txtLog->appendPlainText("  - " + ln);
        ui->txtLog->appendPlainText("");
    }

    QMessageBox::information(this, tr("Karşılaştırma"),
                             QString("Görsel: %1\nTP: %2\nClassMismatch: %3\nFP: %4\nFN: %5\nIoU eşik: %6\nSimilarity: %7 (%8%%)")
                                 .arg(QFileInfo(imgPath).fileName())
                                 .arg(TP).arg(MIS).arg(FP).arg(FN)
                                 .arg(thr,0,'f',2)
                                 .arg(sim,0,'f',3).arg(qRound(sim*100)));
}

void MainWindow::on_btnCompareLI_clicked()
{
    // Önce tek-görsel (o an açık) karşılaştırmayı yap
    compareCurrentImageAgainstLabelImg();

    // === İSTERSEN ALTTA batch karşılaştırmaya da devam edebilir ===

    // IoU eşiği
    QDoubleSpinBox* sb = this->findChild<QDoubleSpinBox*>("sbIou");
    const double iouThr = (sb && sb->value() > 0.0) ? sb->value() : 0.90;

    // Bizim etiket klasörü (UI'dan)
    QString oursDir = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
    if (oursDir.isEmpty() || !QDir(oursDir).exists()) {
        QMessageBox::warning(this, tr("Uyarı"), tr("Önce kendi etiket klasörünü (leLabels) seçin."));
        return;
    }

    // *** DÜZENLEME: "_li" veya "_labelimg" seçiliyse 'ours' ve 'other'ı akıllı ayır ***
    QString otherDir;
    if (oursDir.contains("_li", Qt::CaseInsensitive) ||
        oursDir.contains("_labelimg", Qt::CaseInsensitive))
    {
        // leLabels LabelImg klasörünü gösteriyor → 'other' budur, 'ours'u türet
        otherDir = oursDir;
        QDir od(otherDir);                // .../labels_yolo_li/train
        od.cdUp();                        // .../labels_yolo_li
        QString baseParent = od.dirName();// "labels_yolo_li"
        QString oursParent = baseParent;
        QRegularExpression rx("_li(_labelimg)?", QRegularExpression::CaseInsensitiveOption);
        oursParent.replace(rx, "");       // "labels_yolo"
        od.cdUp();                        // .../(dataset/person)
        oursDir = QDir(od.absolutePath()).filePath(oursParent + "/train");
    } else {
        // leLabels bizim klasörü gösteriyor → diğerini otomatik bul
        otherDir = autoFindOtherLabels(oursDir);
    }

    // Boşsa kullanıcıya sor
    if (otherDir.isEmpty() || !QDir(otherDir).exists()) {
        otherDir = getExistingDirectorySafe(this, tr("Karşı etiket klasörü"), QDir::homePath());
        if (otherDir.isEmpty()) return;
    }

    // Görüntü kökü (XY* piksel ise gerekli)
    const QString leImagesTxt   = ui->leImages ? ui->leImages->text().trimmed() : QString();
    const QString imagesRootDir = normalizeImagesRoot(leImagesTxt);

    QDir A(oursDir), B(otherDir);
    const QStringList Af = A.entryList({"*.txt","*.xml"}, QDir::Files, QDir::Name);
    const QStringList Bf = B.entryList({"*.txt","*.xml"}, QDir::Files, QDir::Name);

    // --- DÜZENLEME: Stem kümesini yalnızca aktif görsele daralt ---
    QString onlyStem;
    if (ui->annotView && !ui->annotView->currentImage().isEmpty()) {
        onlyStem = QFileInfo(ui->annotView->currentImage()).completeBaseName();
    } else if (ui->listFiles && ui->listFiles->currentItem()) {
        onlyStem = QFileInfo(currentFromList(ui->listFiles)).completeBaseName();
    }
    // annotView/listFiles boşsa leLabels .txt'ten stem al
    if (onlyStem.isEmpty()) {
        const QString lbl = ui->leLabels ? ui->leLabels->text().trimmed() : QString();
        if (!lbl.isEmpty() && lbl.endsWith(".txt", Qt::CaseInsensitive))
            onlyStem = QFileInfo(lbl).completeBaseName();
    }

    QSet<QString> stems;
    if (!onlyStem.isEmpty()) {
        stems.insert(onlyStem);   // SADECE açık görsel
    } else {
        // ESKİ DAVRANIŞ: tüm ortak stem’ler
        for (const auto& f : Af) stems.insert(QFileInfo(f).completeBaseName());
        for (const auto& f : Bf) stems.insert(QFileInfo(f).completeBaseName());
    }

    int files=0, tp=0, clsMis=0, fp=0, fn=0;

    if (ui->txtLog) {
        ui->txtLog->appendPlainText("=== Kutu Karşılaştırma (YOLO ↔ XYXY/XYWH/VOC) ===");
        ui->txtLog->appendPlainText("[compare] oursDir  = " + oursDir);
        ui->txtLog->appendPlainText("[compare] otherDir = " + otherDir);
    }

    for (const QString& s : stems) {
        const QString paTxt = A.absoluteFilePath(s + ".txt");
        const QString paXml = A.absoluteFilePath(s + ".xml");
        const QString pbTxt = B.absoluteFilePath(s + ".txt");
        const QString pbXml = B.absoluteFilePath(s + ".xml");

        QString pa, pb;
        if (QFile::exists(paTxt)) pa = paTxt;
        else if (QFile::exists(paXml)) pa = paXml;
        if (QFile::exists(pbTxt)) pb = pbTxt;
        else if (QFile::exists(pbXml)) pb = pbXml;

        if (pa.isEmpty() && pb.isEmpty()) continue;
        ++files;

        // Bizim taraf format
        AnnFmt fmtA = pa.endsWith(".xml", Qt::CaseInsensitive) ? AnnFmt::VOC_XML : AnnFmt::YOLO;

        // Karşı taraf format algıla
        AnnFmt fmtB = AnnFmt::UNKNOWN;
        if (!pb.isEmpty()) {
            if (pb.endsWith(".xml", Qt::CaseInsensitive)) {
                fmtB = AnnFmt::VOC_XML;
            } else {
                QFile fb(pb);
                if (fb.open(QIODevice::ReadOnly|QIODevice::Text)) {
                    QTextStream ts(&fb);
                    QString firstNonEmpty;
                    while (!ts.atEnd() && firstNonEmpty.isEmpty()) {
                        const QString ln = ts.readLine().trimmed();
                        if (!ln.isEmpty()) firstNonEmpty = ln;
                    }
                    fb.close();
                    fmtB = detectFormatFromLine(firstNonEmpty);
                }
            }
        }

        // Görüntü boyutu gerekebilir
        QSize imgSz;
        if (fmtB==AnnFmt::XYXY_PIX || fmtB==AnnFmt::XYWH_PIX) {
            imgSz = findImageSizeForStem(s, imagesRootDir);
        }

        QVector<XYXY> va = readGenericNorm(pa, fmtA, {});
        QVector<XYXY> vb = readGenericNorm(pb, fmtB, imgSz);

        if (va.isEmpty() && vb.isEmpty()) continue;

        QVector<bool> usedB(vb.size(), false);
        int tp_i=0, mis_i=0, fp_i=0, fn_i=0;

        // Greedy eşleştirme (IoU)
        for (int i=0;i<va.size();++i){
            int best=-1; double bestIoU=-1.0;
            for (int j=0;j<vb.size();++j){
                if (usedB[j]) continue;
                const double d = iouXYXY(va[i], vb[j]);
                if (d > bestIoU) { bestIoU = d; best = j; }
            }
            if (best>=0 && bestIoU>=iouThr) {
                usedB[best] = true;
                if (va[i].cls == vb[best].cls) tp_i++;
                else mis_i++;
            } else {
                fp_i++;
            }
        }
        for (int j=0;j<vb.size();++j) if (!usedB[j]) fn_i++;

        tp += tp_i; clsMis += mis_i; fp += fp_i; fn += fn_i;

        if (ui->txtLog) {
            ui->txtLog->appendPlainText(
                QString("[%1] %2  TP=%3  ClassMismatch=%4  FP=%5  FN=%6  (ours=%7, other=%8)  [fmtB=%9]")
                    .arg(files).arg(s).arg(tp_i).arg(mis_i).arg(fp_i).arg(fn_i)
                    .arg(va.size()).arg(vb.size())
                    .arg(int(fmtB))
                );
        }
    }

    // Özet
    const int denom = tp + clsMis + fp + fn;
    const double similarity = (denom > 0) ? double(tp) / double(denom) : 1.0;

    const QString summary = QString(
                                "Dosya: %1\nTP: %2\nClassMismatch: %3\nFP (bizde fazla): %4\n"
                                "FN (karşı tarafta fazla): %5\nIoU eşik: %6\nSimilarity: %7 (%8%%)")
                                .arg(files).arg(tp).arg(clsMis).arg(fp).arg(fn)
                                .arg(iouThr,0,'f',2)
                                .arg(similarity,0,'f',3)
                                .arg(qRound(similarity*100));

    if (ui->txtLog) ui->txtLog->appendPlainText("--- Özet ---\n" + summary + "\n");
    QMessageBox::information(this, tr("Karşılaştırma Özeti"), summary);
}
