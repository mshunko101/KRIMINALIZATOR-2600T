import matplotlib.pyplot as plt
import time
import numpy as np
from sklearn.cluster import KMeans
import threading
import queue
import serial

# ================= НАСТРОЙКИ =================
PORT_SENSOR = '/dev/ttyACM0'
PORT_ACTUATOR = '/dev/ttyUSB0'
BAUD = 9600
BIT_COUNT = 118
SCALE_FACTOR = 2**100

# Физические параметры
PIEZO_THICKNESS_MM = 25.0       
NUM_PIEZOS = 1                  
SPEED_OF_SOUND_M_S = 3000.0     
L_meters = (PIEZO_THICKNESS_MM * NUM_PIEZOS) / 1000.0
f_resonant_calc = 2000.0

# Границы поиска частоты (Гц)
MIN_FREQ = f_resonant_calc * 0.5   # Не опускаемся ниже 50% от резонанса
MAX_FREQ = f_resonant_calc * 1.5   # Не поднимаемся выше 150%

# Параметры оптимизации
INITIAL_STEP = 50.0                # Начальный шаг изменения частоты
MIN_STEP = 1.0                     # Минимальный шаг (точность)
STEP_DECAY = 0.95                  # Коэффициент уменьшения шага при успехе
STEP_GROWTH = 1.1                  # Коэффициент увеличения шага при неудаче
HYSTERESIS_THRESHOLD = 0.8         # Порог чувствительности к изменению кластеров (чтобы не дергаться от шума)

use_emulation = False              
stop_event = threading.Event()
q_freq_update = queue.Queue()
ser_sensor = None
ser_actuator = None

# Состояние системы
current_freq = f_resonant_calc      # Начинаем с расчетного резонанса
freq_step = INITIAL_STEP           # Текущий шаг изменения частоты
direction = 1                      # Направление поиска (+1 вверх, -1 вниз)
last_valid_clusters = None         # Предыдущее количество кластеров для сравнения

def solve_system(y_val):
    if y_val == 0: return 0.0, 0.0
    return (2.0 * y_val) / 3.0, y_val / 3.0

def actuator_thread():
    global ser_actuator, stop_event
    while not stop_event.is_set():
        try:
            new_freq = q_freq_update.get(timeout=0.1)
            # Ограничиваем частоту пределами
            safe_freq = max(MIN_FREQ, min(MAX_FREQ, new_freq))
            
            if ser_actuator and not use_emulation:
                cmd = f"SET_FREQ={safe_freq:.1f}\n"
                ser_actuator.write(cmd.encode('utf-8'))
            elif use_emulation:
                print(f"[Emulation] Setting frequency to {safe_freq:.2f} Hz")
                
        except queue.Empty: 
            pass
        except Exception as e: 
            print(f"Actuator Error: {e}")

def sensor_thread(data_queue, ser_sensor):
    global current_freq, stop_event, use_emulation
    
    while not stop_event.is_set():
        if ser_sensor:
            try:
                ser_sensor.write(b'GET_DATA\n')
                line = ser_sensor.readline().decode('utf-8').strip()
                if len(line) == BIT_COUNT: 
                    data_queue.put(line)
            except Exception as e:
                # print(f"Sensor read error: {e}") # Можно раскомментировать для отладки
                pass
        # Данные приходят 1 раз в секунду, ждем немного меньше, чтобы не блокировать поток
        time.sleep(0.8) 

# ... (код до main) ...

def main():
    global current_freq, freq_step, direction, last_valid_clusters, ser_sensor, ser_actuator
    
    data_queue = queue.Queue(maxsize=60)
    
    if not use_emulation:
        try:
            ser_sensor = serial.Serial(PORT_SENSOR, BAUD, timeout=1)
            ser_actuator = serial.Serial(PORT_ACTUATOR, BAUD, timeout=1)
            time.sleep(2)
        except serial.SerialException as e:
            print(f"❌ Ошибка портов: {e}")
            return
    else:
        print(f"⚠️ ЭМУЛЯЦИЯ ВКЛЮЧЕНА")

    t_sen = threading.Thread(target=sensor_thread, args=(data_queue, ser_sensor), daemon=True)
   
    t_sen.start()

    # === ИНИЦИАЛИЗАЦИЯ ГРАФИКОВ (ДЕЛАЕМ ЭТО ОДИН РАЗ) ===
    plt.ion()
    fig = plt.figure(figsize=(16, 7))
    ax_scatter = fig.add_subplot(1, 2, 1) 
    ax_plot = fig.add_subplot(1, 2, 2) 
    
    # Создаем дополнительные оси СРАЗУ, а не в цикле
    ax2 = ax_plot.twinx()  # Для Inertia
    ax3 = ax_plot.twinx()  # Для Frequency
    
    # Настраиваем положение третьей оси (справа)
    ax3.spines["right"].set_position(("axes", 1.15))
    
    # Линии графиков (создаем пустые, чтобы потом обновлять их данные)
    line_raw, = ax_plot.plot([], [], color='#2c3e50', linewidth=1, label='Raw Clusters')
    line_avg, = ax_plot.plot([], [], color='#e74c3c', linewidth=2, label='Avg Clusters')
    line_inertia, = ax2.plot([], [], color='#f39c12', linestyle='-.', linewidth=1.5, label='Inertia')
    line_freq, = ax3.plot([], [], color='#3498db', linestyle='--', linewidth=1.5, label='Frequency (Hz)')

    # Настройка стилей осей
    ax_plot.set_title('Динамика системы: Оптимизация частоты')
    ax_plot.set_xlabel('Время (сек)')
    ax_plot.grid(True, linestyle=':', alpha=0.4)
    
    ax2.set_ylabel('Inertia', color='#f39c12')
    ax2.tick_params(axis='y', labelcolor='#f39c12')
    
    ax3.set_ylabel('Frequency (Hz)', color='#3498db')
    ax3.tick_params(axis='y', labelcolor='#3498db')

    xs, ys, zs, rhos = [], [], [], []
    cluster_history = []
    inertia_history = [] 
    freq_history = []
    time_history = []
    HISTORY_SIZE = 40
    
    start_time = time.time()
    print(f"\n🚀 ЗАПУСК СИСТЕМЫ ОПТИМИЗАЦИИ")
    time.sleep(1)

    try:
        while True:
            # --- 1. Чтение данных ---
            while not data_queue.empty():
                bit_string = data_queue.get()
                try:
                    y_raw = int(bit_string, 2)
                    x1, x2 = solve_system(y_raw)
                    rho_val = y_raw / SCALE_FACTOR
                    
                    xs.append(x1); ys.append(x2); zs.append(y_raw); rhos.append(rho_val)
                    if len(xs) > 150:
                        xs.pop(0); ys.pop(0); zs.pop(0); rhos.pop(0)
                except ValueError: 
                    pass

            current_inertia = 0.0
            valid_clusters_count = 0
            avg_clusters = 0.0

            # --- 2. Кластеризация ---
            if len(xs) >= 30:
                points = np.column_stack((xs, ys, zs))
                k_attempt = 15 
                
                try:
                    kmeans = KMeans(n_clusters=k_attempt, random_state=42, n_init=10, max_iter=300).fit(points)
                    labels = kmeans.labels_
                    inertia = kmeans.inertia_ 
                    current_inertia = inertia
                    
                    unique, counts = np.unique(labels, return_counts=True)
                    cluster_sizes = dict(zip(unique, counts))
                    total_points = len(labels)
                    
                    valid_count = sum(1 for c_id, count in cluster_sizes.items() if count / total_points > 0.05)
                    valid_clusters_count = valid_count
                    
                    # Обновление истории
                    cluster_history.append(valid_clusters_count)
                    inertia_history.append(current_inertia)
                    freq_history.append(current_freq)
                    time_history.append(time.time() - start_time)
                    
                    if len(cluster_history) > HISTORY_SIZE:
                        cluster_history.pop(0)
                        inertia_history.pop(0)
                        freq_history.pop(0)
                        time_history.pop(0)
                    
                    avg_clusters = sum(cluster_history[-10:]) / len(cluster_history[-10:])

                except Exception as e:
                    print(f"Clustering error: {e}")
                    continue

            # --- 3. Логика оптимизации (как было) ---
            # ... (вставь сюда свой блок с if last_valid_clusters is not None ...) ...
            
            
            # Не забудь обновить last_valid_clusters в конце этого блока
            if last_valid_clusters is not None:
                diff = last_valid_clusters - valid_clusters_count
                if diff > HYSTERESIS_THRESHOLD:
                    freq_step *= STEP_DECAY
                    freq_step = max(freq_step, MIN_STEP)
                    new_freq = current_freq + (direction * freq_step)
                elif diff < -HYSTERESIS_THRESHOLD:
                    direction *= -1
                    freq_step *= STEP_GROWTH
                    new_freq = current_freq + (direction * freq_step)
                else:
                    freq_step *= 1.01 
                    new_freq = current_freq + (direction * freq_step)

                new_freq = max(MIN_FREQ, min(MAX_FREQ, new_freq))
                if abs(new_freq - current_freq) > 0.1:
                    current_freq = new_freq
                    q_freq_update.put(current_freq)

            last_valid_clusters = valid_clusters_count

            # Вывод в консоль
            status_msg = f"\r📊 Freq: {current_freq:7.1f}Hz | Clusters: {avg_clusters:6.4f} | Inertia: {current_inertia:.2e} | Step: {freq_step:.2f}"
            print(status_msg, end='', flush=True)

            # --- 4. ОТРИСОВКА (ТЕПЕРЬ БЕЗ СОЗДАНИЯ НОВЫХ ОСЕЙ) ---
            
            # Очистка только основного поля scatter (если нужно), оси трогать не надо!
            ax_scatter.clear()
            
            if xs:
                sc = ax_scatter.scatter(xs, ys, c=zs, cmap='turbo', alpha=0.8, s=40, edgecolors='none')
                info_text = (f'Freq: {current_freq:.1f} Hz\n'
                             f'Valid Clusters Avg: {avg_clusters:.4f}\n'
                             f'Inertia: {current_inertia:.1e}\n'
                             f'Dir: {"+" if direction > 0 else "-"} | Step: {freq_step:.1f}')
                ax_scatter.text(0.02, 0.95, info_text, transform=ax_scatter.transAxes, 
                                fontsize=10, verticalalignment='top', 
                                bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.9))
                ax_scatter.set_title('Пространство состояний')
                ax_scatter.set_xlabel('X'); ax_scatter.set_ylabel('Y')
            else:
                ax_scatter.text(0.5, 0.5, 'Ожидание данных...', transform=ax_scatter.transAxes)

            # Обновляем данные линий, если есть история
            if len(time_history) > 5:
                # Считаем скользящее среднее для отображения
                rolling_avg = []
                for i in range(len(cluster_history)):
                    window = cluster_history[max(0, i-5):i+1]
                    rolling_avg.append(sum(window)/len(window))
                
                # САМОЕ ВАЖНОЕ: обновляем данные существующих линий вместо создания новых
                line_raw.set_data(time_history, cluster_history)
                line_avg.set_data(time_history, rolling_avg)
                line_inertia.set_data(time_history, inertia_history)
                line_freq.set_data(time_history, freq_history)

                # Автомасштабирование осей (чтобы график не уезжал за пределы видимости)
                ax_plot.relim(); ax_plot.autoscale_view()
                ax2.relim(); ax2.autoscale_view()
                ax3.relim(); ax3.autoscale_view()

                # Пересборка легенды (теперь она строится корректно, так как оси не дублируются)
                lines_1, labels_1 = ax_plot.get_legend_handles_labels()
                lines_2, labels_2 = ax2.get_legend_handles_labels()
                lines_3, labels_3 = ax3.get_legend_handles_labels()
                
                # Удаляем старую легенду, если она есть, чтобы не дублировалась
                legend = ax_plot.legend(lines_1 + lines_2 + lines_3, labels_1 + labels_2 + labels_3, loc='upper left')
                legend.set_draggable(True) # Опционально: можно таскать мышкой

            plt.draw()
            plt.pause(0.001)
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\n🛑 Стоп пользователем.")
    finally:
        stop_event.set()
        if ser_sensor: ser_sensor.close()
        if ser_actuator: ser_actuator.close()
        plt.close()

if __name__ == "__main__":
    main()


