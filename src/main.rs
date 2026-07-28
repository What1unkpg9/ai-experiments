use eframe::egui;
use std::io::Read;
use std::path::PathBuf;
use std::sync::mpsc::{channel, Receiver, Sender};
use std::thread;

#[derive(PartialEq, Clone, Copy)]
enum HashAlgo {
    Crc32,
    Md5,
    Sha1,
    Sha256,
    Sha384,
}

impl HashAlgo {
    fn name(&self) -> &'static str {
        match self {
            HashAlgo::Crc32 => "CRC32",
            HashAlgo::Md5 => "MD5",
            HashAlgo::Sha1 => "SHA-1",
            HashAlgo::Sha256 => "SHA-256",
            HashAlgo::Sha384 => "SHA-384",
        }
    }
}

struct ChecksumApp {
    selected_file: Option<PathBuf>,
    algo: HashAlgo,
    calculated_hash: String,
    expected_hash: String,
    is_calculating: bool,
    tx: Sender<String>,
    rx: Receiver<String>,
}

impl Default for ChecksumApp {
    fn default() -> Self {
        let (tx, rx) = channel();
        Self {
            selected_file: None,
            algo: HashAlgo::Sha256,
            calculated_hash: String::new(),
            expected_hash: String::new(),
            is_calculating: false,
            tx,
            rx,
        }
    }
}

impl ChecksumApp {
    fn start_calculation(&mut self) {
        if let Some(path) = &self.selected_file {
            self.is_calculating = true;
            self.calculated_hash.clear();

            let path = path.clone();
            let algo = self.algo;
            let tx = self.tx.clone();

            println!("[LOG] Начат просчет файла: {:?}", path.file_name().unwrap_or_default());
            println!("[LOG] Выбран алгоритм: {}", algo.name());

            thread::spawn(move || {
                let result = compute_file_hash(&path, algo);
                let _ = tx.send(result);
            });
        }
    }
}

fn compute_file_hash(path: &PathBuf, algo: HashAlgo) -> String {
    let mut file = match std::fs::File::open(path) {
        Ok(f) => f,
        Err(e) => {
            let err_msg = format!("Ошибка открытия файла: {}", e);
            println!("[ERROR] {}", err_msg);
            return err_msg;
        }
    };

    let mut buffer = [0u8; 65536];

    let hash_str = match algo {
        HashAlgo::Crc32 => {
            let mut hasher = crc32fast::Hasher::new();
            loop {
                match file.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(n) => hasher.update(&buffer[..n]),
                    Err(e) => return format!("Ошибка чтения: {}", e),
                }
            }
            format!("{:08x}", hasher.finalize())
        }
        HashAlgo::Md5 => {
            use md5::{Digest, Md5};
            let mut hasher = Md5::new();
            loop {
                match file.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(n) => hasher.update(&buffer[..n]),
                    Err(e) => return format!("Ошибка чтения: {}", e),
                }
            }
            hex::encode(hasher.finalize())
        }
        HashAlgo::Sha1 => {
            use sha1::{Digest, Sha1};
            let mut hasher = Sha1::new();
            loop {
                match file.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(n) => hasher.update(&buffer[..n]),
                    Err(e) => return format!("Ошибка чтения: {}", e),
                }
            }
            hex::encode(hasher.finalize())
        }
        HashAlgo::Sha256 => {
            use sha2::{Digest, Sha256};
            let mut hasher = Sha256::new();
            loop {
                match file.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(n) => hasher.update(&buffer[..n]),
                    Err(e) => return format!("Ошибка чтения: {}", e),
                }
            }
            hex::encode(hasher.finalize())
        }
        HashAlgo::Sha384 => {
            use sha2::{Digest, Sha384};
            let mut hasher = Sha384::new();
            loop {
                match file.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(n) => hasher.update(&buffer[..n]),
                    Err(e) => return format!("Ошибка чтения: {}", e),
                }
            }
            hex::encode(hasher.finalize())
        }
    };

    println!("[SUCCESS] Хэш успешно вычислен: {}", hash_str);
    hash_str
}

impl eframe::App for ChecksumApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if let Ok(hash) = self.rx.try_recv() {
            self.calculated_hash = hash;
            self.is_calculating = false;
        }

        // Drag-and-Drop
        if !ctx.input(|i| i.raw.dropped_files.is_empty()) {
            if let Some(dropped) = ctx.input(|i| i.raw.dropped_files.first().cloned()) {
                if let Some(path) = dropped.path {
                    println!("\n[EVENT] Перетащен новый файл!");
                    self.selected_file = Some(path);
                    self.start_calculation();
                }
            }
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("⚡ Checksum Comparator");
            ui.add_space(8.0);

            // Переключатель алгоритмов
            ui.horizontal(|ui| {
                ui.label("Алгоритм:");
                for algo in [
                    HashAlgo::Crc32,
                    HashAlgo::Md5,
                    HashAlgo::Sha1,
                    HashAlgo::Sha256,
                    HashAlgo::Sha384,
                ] {
                    if ui.selectable_value(&mut self.algo, algo, algo.name()).clicked() {
                        if self.selected_file.is_some() {
                            println!("\n[EVENT] Переключен алгоритм на {}", algo.name());
                            self.start_calculation();
                        }
                    }
                }
            });

            ui.add_space(12.0);

            // Серый квадрат для файла
            let drop_zone_height = 100.0;
            let (rect, response) = ui.allocate_exact_size(
                egui::vec2(ui.available_width(), drop_zone_height),
                egui::Sense::click(),
            );

            let is_hovered = response.hovered();
            let bg_color = if is_hovered {
                egui::Color32::from_rgb(45, 55, 72)
            } else {
                egui::Color32::from_rgb(26, 32, 44)
            };

            ui.painter().rect_filled(rect, 8.0, bg_color);
            ui.painter().rect_stroke(
                rect,
                8.0,
                egui::Stroke::new(1.5, egui::Color32::from_rgb(74, 85, 104)),
            );

            let text = match &self.selected_file {
                Some(p) => format!("📄 Выбран файл: {}", p.file_name().unwrap_or_default().to_string_lossy()),
                None => "📂 Перетащите файл сюда или кликните для выбора".to_string(),
            };

            ui.painter().text(
                rect.center(),
                egui::Align2::CENTER_CENTER,
                text,
                egui::FontId::proportional(15.0),
                egui::Color32::WHITE,
            );

            if response.clicked() {
                if let Some(path) = rfd::FileDialog::new().pick_file() {
                    println!("\n[EVENT] Файл выбран через проводник");
                    self.selected_file = Some(path);
                    self.start_calculation();
                }
            }

            ui.add_space(15.0);

            // Вывод хэша
            ui.group(|ui| {
                ui.set_width(ui.available_width());
                ui.label(egui::RichText::new("Полученная контрольная сумма:").strong());

                if self.is_calculating {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(" Считаем хэш...");
                    });
                } else if !self.calculated_hash.is_empty() {
                    ui.text_edit_singleline(&mut self.calculated_hash.as_str());
                } else {
                    ui.label("Ожидание файла...");
                }
            });

            ui.add_space(10.0);

            // Поле для вставки с сайта
            ui.group(|ui| {
                ui.set_width(ui.available_width());
                ui.label(egui::RichText::new("Ожидаемая сумма (с сайта):").strong());
                ui.text_edit_singleline(&mut self.expected_hash);

                ui.add_space(5.0);

                if !self.calculated_hash.is_empty() && !self.expected_hash.trim().is_empty() {
                    let calc = self.calculated_hash.trim().to_lowercase();
                    let exp = self.expected_hash.trim().to_lowercase();

                    if calc == exp {
                        ui.colored_label(
                            egui::Color32::GREEN,
                            "✅ Контрольные суммы СОВПАДАЮТ! Файл целостен.",
                        );
                    } else {
                        ui.colored_label(
                            egui::Color32::RED,
                            "❌ Суммы НЕ СОВПАДАЮТ! Файл поврежден или подменен.",
                        );
                    }
                }
            });
        });
    }
}

fn main() -> eframe::Result<()> {
    println!("=============================================");
    println!("   Checksum Comparator by What1unkpg9       ");
    println!("=============================================");
    println!("[SYS] Запуск графического интерфейса...");

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([550.0, 420.0])
            .with_resizable(false),
        ..Default::default()
    };

    eframe::run_native(
        "Checksum Comparator (Rust UI)",
        options,
        Box::new(|_cc| Box::new(ChecksumApp::default())),
    )
}
