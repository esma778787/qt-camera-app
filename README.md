Bu proje, canlı kamera görüntüsünden ya da mevcut görsel veri setlerinden YOLO ve Pascal VOC formatlarında etiket üretmeye olanak tanıyan bir görsel etiketleme (annotation) aracıdır.
Araç; veri toplama, etiketleme ve derin öğrenme modellerinin (YOLO, Faster R-CNN vb.) eğitimine doğrudan uygun veri seti oluşturmak için geliştirilmiştir.

🚀 Temel Özellikler

📷 Kamera ile Etiketleme: USB kamera veya dahili kamera ile gerçek zamanlı görüntü alarak etiketleme

📂 Klasör Tabanlı Çalışma: Klasörden toplu görsel yükleme

🔲 Kutu (Bounding Box) Çizimi: Fare ile kutu çizme, taşıma ve yeniden boyutlandırma

🏷️ Çoklu Sınıf Desteği: classes.txt üzerinden sınıfları yükleme ve hızlı seçim

💾 Çift Format Çıktısı: YOLO (.txt) ve Pascal VOC (.xml) formatında etiket kaydetme

🔄 Eğitime Hazır Veri Seti: Üretilen etiketler doğrudan eğitim pipeline’ında kullanılabilir

⚡ Performans: Qt’nin çok-iş parçacıklı (multi-threaded) yapısı ile canlı kamera akışında yüksek performans

🔗 Python & C++ Hibrit Yapısı: Qt (C++) arayüzü ve Python tabanlı veri işleme entegrasyonu

🛠️ Kullanılan Teknolojiler

Qt6 / C++17 – Kullanıcı arayüzü ve kutu çizim motoru

Python 3.x – Veri işleme, dönüştürme ve format yönetimi

OpenCV – Görüntü işleme ve kamera entegrasyonu

JSON / XML – Konfigürasyon ve VOC formatı için etiketleme

YOLO & Pascal VOC – Çıktı formatları
