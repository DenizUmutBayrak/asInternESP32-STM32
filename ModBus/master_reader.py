import tkinter as tk
from tkinter import ttk
from pymodbus.client import ModbusTcpClient
import time
import configparser
import sys

# --- Yapılandırma Dosyasından Ayarları Oku ---
config = configparser.ConfigParser()
try:
    config.read('config.ini')

    # Modbus Ayarları
    SLAVE_IPS = [ip.strip() for ip in config['Modbus']['slave_ips'].split(',')]
    MODBUS_PORT = int(config['Modbus']['modbus_port'])
    POLLING_INTERVAL_MS = int(config['Modbus']['polling_interval_ms'])

    # Güvenlik Ayarları
    SECRET_KEY = int(config['Security']['secret_key'])

except KeyError as ke:
    print(f"Hata: config.ini dosyasında eksik veya hatalı bölüm/anahtar: {ke}")
    print("Lütfen config.ini dosyasının doğru formatta olduğundan ve tüm gerekli anahtarları içerdiğinden emin olun.")
    SLAVE_IPS = ['192.168.220.179']  # Varsayılan olarak sadece bir IP
    MODBUS_PORT = 502
    POLLING_INTERVAL_MS = 2000
    SECRET_KEY = 12345
    print("Varsayılan ayarlar kullanılıyor. Lütfen config.ini dosyanızı kontrol edin.")
except Exception as ex:
    print(f"config.ini dosyasını okurken beklenmeyen bir hata oluştu: {ex}")
    SLAVE_IPS = ['192.168.220.179']  # Varsayılan olarak sadece bir IP
    MODBUS_PORT = 502
    POLLING_INTERVAL_MS = 2000
    SECRET_KEY = 12345
    print("Varsayılan ayarlar kullanılıyor. Lütfen config.ini dosyanızı kontrol edin.")


# --- Tkinter Arayüzü Oluşturma ---
class ModbusMonitorApp:
    def __init__(self, master):
        self.master = master
        master.title("Trafo Odası Nem/Sıcaklık Monitörü")

        self.num_slaves = len(SLAVE_IPS)
        # Pencere boyutunu slave_ips sayısına ve her cihazın veri tipine göre ayarla
        # İlk cihaz 2 veri (Nem, Sıcaklık), İkinci cihaz 1 veri (Sıcaklık)
        total_height = 100  # Başlık ve boşluklar için
        for i, ip in enumerate(SLAVE_IPS):
            if i == 0:  # İlk ESP (DHT11) için
                total_height += 120  # Nem ve Sıcaklık etiketi için
            elif i == 1:  # İkinci ESP (DS18B20) için
                total_height += 90  # Sadece Sıcaklık etiketi için
            else:  # Eğer gelecekte daha fazla cihaz eklenirse
                total_height += 100

        master.geometry(f"600x{total_height}")
        master.resizable(False, False)

        master.protocol("WM_DELETE_WINDOW", self.clean_up)

        self.style = ttk.Style()
        self.style.configure('TFrame', background='#e0e0e0')
        self.style.configure('TLabel', background='#e0e0e0', font=('Arial', 12))
        self.style.configure('Data.TLabel', background='#f0f0f0', font=('Arial', 14, 'bold'), foreground='#000080',
                             padding=5)
        self.style.configure('Header.TLabel', background='#c0c0c0', font=('Arial', 14, 'bold'), foreground='black',
                             padding=5)
        self.style.configure('Status.TLabel', font=('Arial', 10), foreground='gray')

        self.slave_frames = []
        self.data_labels = {}
        self.is_running = True

        for i, ip in enumerate(SLAVE_IPS):
            frame = ttk.LabelFrame(master, text=f"Trafo Odası Cihaz {i + 1} ({ip})", padding=10)
            frame.pack(padx=10, pady=5, fill='x', expand=True)
            self.slave_frames.append(frame)

            # Bu kısım dinamik olarak oluşturulmalı
            labels_for_this_slave = {}
            if i == 0:  # İlk cihaz (DHT11): Nem ve Sıcaklık
                ttk.Label(frame, text="Nem (%RH):", style='TLabel').grid(row=0, column=0, padx=5, pady=2, sticky='w')
                labels_for_this_slave['humidity'] = ttk.Label(frame, text="Yükleniyor...", style='Data.TLabel')
                labels_for_this_slave['humidity'].grid(row=0, column=1, padx=5, pady=2, sticky='ew')

                ttk.Label(frame, text="Sıcaklık (°C):", style='TLabel').grid(row=1, column=0, padx=5, pady=2,
                                                                             sticky='w')
                labels_for_this_slave['temperature'] = ttk.Label(frame, text="Yükleniyor...", style='Data.TLabel')
                labels_for_this_slave['temperature'].grid(row=1, column=1, padx=5, pady=2, sticky='ew')

                # DS18B20 etiketi bu cihazda olmayacak
                labels_for_this_slave['ds18b20_temperature'] = None
                status_row = 2

            elif i == 1:  # İkinci cihaz (DS18B20): Sadece Sıcaklık
                ttk.Label(frame, text="Sıcaklık (DS18B20 °C):", style='TLabel').grid(row=0, column=0, padx=5, pady=2,
                                                                                     sticky='w')
                labels_for_this_slave['ds18b20_temperature'] = ttk.Label(frame, text="Yükleniyor...",
                                                                         style='Data.TLabel')
                labels_for_this_slave['ds18b20_temperature'].grid(row=0, column=1, padx=5, pady=2, sticky='ew')

                # DHT etiketleri bu cihazda olmayacak
                labels_for_this_slave['humidity'] = None
                labels_for_this_slave['temperature'] = None
                status_row = 1

            # Durum etiketi her zaman en altta
            labels_for_this_slave['status'] = ttk.Label(frame, text="Durum: Bekleniyor...", style='Status.TLabel')
            labels_for_this_slave['status'].grid(row=status_row, column=0, columnspan=2, padx=5, pady=2, sticky='w')

            self.data_labels[ip] = labels_for_this_slave

        self.update_data()

    def update_data(self):
        """Her bir slave'den veri okur ve arayüzü günceller."""
        if not self.is_running:
            return

        for i, ip in enumerate(SLAVE_IPS):
            # İlgili etiketleri dictionary'den al
            labels = self.data_labels[ip]
            humidity_label = labels.get('humidity')
            dht_temp_label = labels.get('temperature')  # DHT sıcaklık etiketi için 'temperature' kullanıldı
            ds18b20_temp_label = labels.get('ds18b20_temperature')
            status_label = labels['status']

            client = ModbusTcpClient(ip, port=MODBUS_PORT, timeout=5)

            max_connection_attempts_time = 10
            start_time = time.time()
            connected = False

            while time.time() - start_time < max_connection_attempts_time and not connected:
                try:
                    if client.connect():
                        connected = True
                        break
                    else:
                        status_label.config(text=f"Durum: Bağlanılıyor ({ip})...", foreground="orange")
                        time.sleep(0.2)
                except Exception as e:
                    status_label.config(text=f"Durum: Bağlantı Hatası ({ip}) - {e}", foreground="red")
                    time.sleep(0.2)

            if not connected:
                # KESİNTİ durumunda ilgili etiketleri güncelle
                if humidity_label: humidity_label.config(text="KESİNTİ", foreground='red')
                if dht_temp_label: dht_temp_label.config(text="KESİNTİ", foreground='red')
                if ds18b20_temp_label: ds18b20_temp_label.config(text="KESİNTİ", foreground='red')
                status_label.config(text=f"Durum: Bağlantı Kesintisi ({ip})", foreground="red")
                try:
                    client.close()
                except Exception:
                    pass
                continue  # Bir sonraki IP'ye geç

            # Bağlantı kurulduysa devam et
            try:
                write_result = client.write_register(address=99, value=SECRET_KEY)

                if write_result.isError():
                    status_label.config(text=f"Durum: Anahtar Yazma Hatası ({write_result})", foreground="red")
                    client.close()
                    continue

                status_label.config(text="Durum: Bağlı, Veri Okuyor...", foreground="blue")

                if i == 0:  # İlk cihaz (DHT11 - Nem ve Sıcaklık)
                    result = client.read_holding_registers(address=0, count=2)  # Nem(0), DHT Sıcaklık(1)
                    if not result.isError():
                        humidity = result.registers[0]
                        dht_temperature = result.registers[1]

                        humidity_label.config(text=f"{humidity} %RH", foreground='#000080')
                        dht_temp_label.config(text=f"{dht_temperature} °C", foreground='#000080')
                        status_label.config(text="Durum: Veri OK", foreground="green")
                    else:
                        # DHT etiketleri varsa güncelle
                        if humidity_label: humidity_label.config(text="HATA", foreground='red')
                        if dht_temp_label: dht_temp_label.config(text="HATA", foreground='red')
                        status_label.config(text=f"Durum: Okuma Hatası ({result})", foreground="red")

                elif i == 1:  # İkinci cihaz (DS18B20 - Sadece Sıcaklık)
                    result = client.read_holding_registers(address=0, count=1)  # DS18B20 Sıcaklık(0)
                    if not result.isError():
                        ds18b20_temperature = result.registers[0]

                        ds18b20_temp_label.config(text=f"{ds18b20_temperature} °C", foreground='#000080')
                        status_label.config(text="Durum: Veri OK", foreground="green")
                    else:
                        # DS18B20 etiketi varsa güncelle
                        if ds18b20_temp_label: ds18b20_temp_label.config(text="HATA", foreground='red')
                        status_label.config(text=f"Durum: Okuma Hatası ({result})", foreground="red")

                # Diğer cihaz tipleri için buraya ek if/elif blokları gelebilir

            except Exception as ex:
                if humidity_label: humidity_label.config(text="AĞ HATASI", foreground='red')
                if dht_temp_label: dht_temp_label.config(text="AĞ HATASI", foreground='red')
                if ds18b20_temp_label: ds18b20_temp_label.config(text="AĞ HATASI", foreground='red')
                status_label.config(text=f"Durum: Ağ Hatası ({ex})", foreground="red")
            finally:
                try:
                    client.close()
                except Exception:
                    pass

        if self.is_running:
            self.master.after(POLLING_INTERVAL_MS, self.update_data)

    def clean_up(self):
        """Uygulama kapatılırken çalışan görevleri durdur."""
        print("Uygulama kapatılıyor. Arka plan görevleri sonlandırılıyor...")
        self.is_running = False
        self.master.destroy()
        sys.exit()


if __name__ == "__main__":
    root = tk.Tk()
    app = ModbusMonitorApp(root)
    root.mainloop()