import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time


class FanControlGUI:
    def __init__(self, master):
        self.master = master
        self.master.title("STM32 Fan Kontrol Arayüzü")
        self.master.geometry("500x550")
        self.serial_port = None
        self.serial_thread = None
        self.read_queue = queue.Queue()
        self.running = False
        self.pwm_value = 0  # Manuel mod için PWM değerini saklar

        self.setup_ui()

    def setup_ui(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TButton', font=('Helvetica', 10), padding=8)
        style.configure('TLabel', font=('Helvetica', 10))
        style.configure('TCombobox', font=('Helvetica', 10))

        # Aktif butonu vurgulamak için özel stil
        style.configure('Active.TButton', background='green', foreground='white')
        style.map('Active.TButton',
                  background=[('pressed', 'darkgreen'), ('active', 'darkgreen')])

        # Pasif butonu için normal stil
        style.configure('Inactive.TButton', background='lightgrey', foreground='black')
        style.map('Inactive.TButton',
                  background=[('pressed', 'grey'), ('active', 'grey')])

        # Progressbar için özel stil
        style.configure("TProgressbar",
                        background="green",
                        troughcolor="lightgrey",
                        borderwidth=1,
                        relief="flat")

        port_frame = ttk.LabelFrame(self.master, text="Seri Port Ayarları", padding=10)
        port_frame.pack(pady=10, padx=20, fill="x")

        ttk.Label(port_frame, text="Port Seçin:").grid(row=0, column=0, padx=5, pady=5, sticky="w")
        self.port_combobox = ttk.Combobox(port_frame, width=30)
        self.port_combobox.grid(row=0, column=1, padx=5, pady=5, sticky="ew")
        self.refresh_ports()

        self.connect_button = ttk.Button(port_frame, text="Bağlan", command=self.connect_serial)
        self.connect_button.grid(row=0, column=2, padx=5, pady=5)
        self.disconnect_button = ttk.Button(port_frame, text="Bağlantıyı Kes", command=self.disconnect_serial,
                                            state=tk.DISABLED)
        self.disconnect_button.grid(row=0, column=3, padx=5, pady=5)

        mode_frame = ttk.LabelFrame(self.master, text="Fan Modu Kontrolü", padding=10)
        mode_frame.pack(pady=10, padx=20, fill="x")

        self.current_mode_label = ttk.Label(mode_frame, text="Mevcut Mod: Bağlantı Yok", font=('Helvetica', 12, 'bold'))
        self.current_mode_label.pack(pady=5)
        self.temp_label = ttk.Label(mode_frame, text="Sıcaklık: -- °C", font=('Helvetica', 12, 'bold'))
        self.temp_label.pack(pady=5)

        self.auto_button = ttk.Button(mode_frame, text="Otomatik Mod", command=self.set_auto_mode, state=tk.DISABLED,
                                      style='Inactive.TButton')
        self.auto_button.pack(side=tk.LEFT, expand=True, padx=5, pady=5)
        self.manual_button = ttk.Button(mode_frame, text="Manuel Mod", command=self.set_manual_mode, state=tk.DISABLED,
                                        style='Inactive.TButton')
        self.manual_button.pack(side=tk.RIGHT, expand=True, padx=5, pady=5)

        pwm_frame = ttk.LabelFrame(self.master, text="Manuel PWM Kontrolü (%)", padding=10)
        pwm_frame.pack(pady=10, padx=20, fill="x")

        self.pwm_value_label = ttk.Label(pwm_frame, text="PWM Değeri: 0 %")
        self.pwm_value_label.pack(pady=5)

        # Progressbar oluşturulurken state ayarı kaldırıldı
        self.pwm_progressbar = ttk.Progressbar(pwm_frame, orient="horizontal", length=400, mode="determinate")
        self.pwm_progressbar.pack(pady=10)

        # Başlangıçta Progressbar'ın tıklama olayını bağlama
        # Bağlantı kurulana ve manuel moda geçilene kadar tıklanamaz olacak
        self.pwm_progressbar.bind("<Button-1>", self.on_progressbar_click)  # Bağlama
        self.pwm_progressbar.unbind("<Button-1>")  # Hemen kaldır, başlangıçta pasif olsun

        log_frame = ttk.LabelFrame(self.master, text="Seri Port Logu", padding=10)
        log_frame.pack(pady=10, padx=20, fill="both", expand=True)
        self.log_text = tk.Text(log_frame, height=10, width=50, state='disabled', font=('Consolas', 9))
        self.log_text.pack(fill="both", expand=True)

        self.master.protocol("WM_DELETE_WINDOW", self.on_closing)
        self.master.after(100, self.process_serial_queue)

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_names = [port.device for port in ports]
        self.port_combobox['values'] = port_names
        if port_names:
            self.port_combobox.set(port_names[0])
        else:
            self.port_combobox.set("Port Yok")

    def connect_serial(self):
        port_name = self.port_combobox.get()
        if port_name and port_name != "Port Yok":
            try:
                self.serial_port = serial.Serial(port_name, 115200, timeout=1)
                self.log_message(f"Seri porta başarıyla bağlandı: {port_name}")
                self.update_ui_on_connect(True)
                self.current_mode_label.config(text="Mevcut Mod: Manuel Mod Başlatılıyor...")
                self.start_serial_read_thread()
                self.send_command_to_mcu("M")
            except serial.SerialException as e:
                messagebox.showerror("Bağlantı Hatası", f"Seri porta bağlanılamadı: {e}")
                self.update_ui_on_connect(False)
        else:
            messagebox.showwarning("Uyarı", "Lütfen geçerli bir port seçin.")

    def disconnect_serial(self):
        if self.serial_port and self.serial_port.is_open:
            self.running = False
            if self.serial_thread:
                self.serial_thread.join(timeout=1)
            self.serial_port.close()
            self.log_message("Seri port bağlantısı kesildi.")
            self.update_ui_on_connect(False)

    def update_ui_on_connect(self, connected):
        self.port_combobox.config(state=tk.DISABLED if connected else 'readonly')
        self.connect_button.config(state=tk.DISABLED if connected else tk.NORMAL)
        self.disconnect_button.config(state=tk.NORMAL if connected else tk.DISABLED)
        self.auto_button.config(state=tk.NORMAL if connected else tk.DISABLED)
        self.manual_button.config(state=tk.NORMAL if connected else tk.DISABLED)
        if not connected:
            self.current_mode_label.config(text="Mevcut Mod: Bağlantı Yok")
            self.temp_label.config(text="Sıcaklık: -- °C")
            self.pwm_progressbar.config(value=0)  # Sadece değeri sıfırla
            self.pwm_progressbar.unbind("<Button-1>")  # Bağlantı kesildiğinde tıklama olayını kaldır
            self.pwm_value_label.config(text="PWM Değeri: 0 %")
            self.auto_button.config(style='Inactive.TButton')
            self.manual_button.config(style='Inactive.TButton')

    def start_serial_read_thread(self):
        self.running = True
        self.serial_thread = threading.Thread(target=self.read_from_serial, daemon=True)
        self.serial_thread.start()

    def read_from_serial(self):
        while self.running and self.serial_port and self.serial_port.is_open:
            try:
                line = self.serial_port.readline().decode('utf-8').strip()
                if line:
                    self.read_queue.put(line)
            except serial.SerialTimeoutException:
                pass
            except Exception as e:
                if self.running:
                    self.read_queue.put(f"Hata (Seri Okuma): {e}")
                self.running = False
                break

    def process_serial_queue(self):
        while not self.read_queue.empty():
            message = self.read_queue.get_nowait()
            self.log_message(f"MCU: {message}")
            self.process_incoming_message(message)
        self.master.after(100, self.process_serial_queue)

    def process_incoming_message(self, message):
        if "OK: AUTO MODE" in message:
            self.current_mode_label.config(text="Mevcut Mod: Otomatik")
            self.pwm_progressbar.unbind("<Button-1>")  # Otomatik modda tıklama olayını kaldır
            self.auto_button.config(style='Active.TButton')
            self.manual_button.config(style='Inactive.TButton')
        elif "OK: MANUAL MODE" in message:
            self.current_mode_label.config(text="Mevcut Mod: Manuel")
            self.pwm_progressbar.bind("<Button-1>", self.on_progressbar_click)  # Manuel modda tıklama olayını bağla
            self.auto_button.config(style='Inactive.TButton')
            self.manual_button.config(style='Active.TButton')
        elif "OK: P=" in message:
            try:
                percentage_str = message.split("OK: P=")[1].split(",")[0]
                percentage_value = int(percentage_str)
                self.pwm_progressbar['value'] = percentage_value
                self.pwm_value = percentage_value
                self.pwm_value_label.config(text=f"PWM Değeri: {percentage_value} %")
            except (ValueError, IndexError):
                self.log_message(f"Hata: Geçersiz yüzdelik değeri alındı: {message}")
        elif message.startswith("STATUS: AUTO"):
            try:
                parts = message.split(", ")
                temp_part = parts[1].split("=")[1].replace(" C", "")
                percentage_part = parts[2].split("=")[1]

                self.current_mode_label.config(text="Mevcut Mod: Otomatik")
                self.temp_label.config(text=f"Sıcaklık: {temp_part} °C")
                self.pwm_progressbar['value'] = int(percentage_part)
                self.pwm_value = int(percentage_part)
                self.pwm_value_label.config(text=f"PWM Değeri: {percentage_part} %")
                self.pwm_progressbar.unbind("<Button-1>")  # Otomatik modda tıklama olayını kaldır
                self.auto_button.config(style='Active.TButton')
                self.manual_button.config(style='Inactive.TButton')
            except (ValueError, IndexError):
                self.log_message("Hata: Geçersiz STATUS: AUTO mesajı alındı.")
        elif message.startswith("STATUS: MANUAL"):
            try:
                parts = message.split(", ")
                percentage_part = parts[1].split("=")[1]
                self.current_mode_label.config(text="Mevcut Mod: Manuel")
                self.pwm_progressbar['value'] = int(percentage_part)
                self.pwm_value = int(percentage_part)
                self.pwm_value_label.config(text=f"PWM Değeri: {percentage_part} %")
                self.pwm_progressbar.bind("<Button-1>", self.on_progressbar_click)  # Manuel modda tıklama olayını bağla
                self.auto_button.config(style='Inactive.TButton')
                self.manual_button.config(style='Active.TButton')
            except (ValueError, IndexError):
                self.log_message("Hata: Geçersiz STATUS: MANUAL mesajı alındı.")
        elif "ERROR" in message:
            messagebox.showerror("STM32 Hatası", message)
        elif "OK: Motor Direction ->" in message:
            pass

    def log_message(self, message):
        self.log_text.config(state='normal')
        self.log_text.insert(tk.END, message + '\n')
        self.log_text.see(tk.END)
        self.log_text.config(state='disabled')

    def send_command_to_mcu(self, command):
        if self.serial_port and self.serial_port.is_open:
            try:
                self.log_message(f"GUI -> MCU: {command}")
                self.serial_port.write(f"{command}\n".encode('utf-8'))
                time.sleep(0.1)
            except Exception as e:
                self.log_message(f"Hata: Komut gönderilemedi: {e}")
                self.disconnect_serial()
        else:
            messagebox.showerror("Bağlantı Yok", "Lütfen önce seri porta bağlanın.")

    def set_auto_mode(self):
        self.send_command_to_mcu("A")

    def set_manual_mode(self):
        self.send_command_to_mcu("M")

    # Bu fonksiyon artık kullanılmıyor, çünkü Progressbar'ın "command" seçeneği yok.
    # def update_pwm_from_slider(self, value):
    #     pass

    # Progressbar'a tıklama olayını işleyen yeni fonksiyon
    def on_progressbar_click(self, event):
        # Tıklanan noktanın x koordinatı
        click_x = event.x
        # Progressbar'ın toplam genişliği
        widget_width = self.pwm_progressbar.winfo_width()

        # Yüzdelik değeri hesapla
        new_percentage = int((click_x / widget_width) * 100)

        # Değerin 0-100 arasında kalmasını sağla
        if new_percentage < 0: new_percentage = 0
        if new_percentage > 100: new_percentage = 100

        # UI'ı ve MCU'ya komutu gönder
        self.pwm_progressbar['value'] = new_percentage
        self.pwm_value = new_percentage
        self.pwm_value_label.config(text=f"PWM Değeri: {new_percentage} %")
        self.send_command_to_mcu(f"P={new_percentage}")

    def on_closing(self):
        self.disconnect_serial()
        self.master.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = FanControlGUI(root)
    root.mainloop()