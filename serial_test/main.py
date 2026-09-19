from HoverSerial import *
import threading
import time

SPEED_MIN = -300
SPEED_MAX = 300
SPEED_STEP = 2  # [-] Speed step
TIME_SEND = 0.1  # [s] Sending time interval

stop = threading.Event()
target_lock = threading.Lock()
target_speed = 0


def thread_send_command():
    steer = 0
    speed = 0

    while not stop.is_set():
        with target_lock:
            target = target_speed

        if speed < target:
            speed = min(speed + SPEED_STEP, target)
        elif speed > target:
            speed = max(speed - SPEED_STEP, target)

        hover_serial.send_command(steer, speed)
        print('Sending:\t steer: ' + str(steer) + ' speed: ' + str(speed) + ' target: ' + str(target))

        stop.wait(TIME_SEND)


def thread_receive_feedback():
    while not stop.is_set():
        try:
            feedback = hover_serial.receive_feedback()
        except Exception:
            if stop.is_set():
                break
            raise

        if feedback is None:
            continue

        print('Receiving:\t', feedback)


def read_target():
    raw = input('Speed [{}..{}] (q to quit): '.format(SPEED_MIN, SPEED_MAX)).strip()
    if raw.lower() in ('q', 'quit', 'exit'):
        return None
    if raw == '':
        return target_speed
    value = int(raw)
    return max(SPEED_MIN, min(SPEED_MAX, value))


if __name__ == "__main__":
    SERIAL_PORT = 'COM12'
    SERIAL_BAUD = 115200
    hover_serial = Hoverboard_serial(SERIAL_PORT, SERIAL_BAUD)

    thread1 = threading.Thread(target=thread_send_command, daemon=True)
    thread2 = threading.Thread(target=thread_receive_feedback, daemon=True)
    thread1.start()
    thread2.start()

    try:
        while not stop.is_set():
            try:
                value = read_target()
            except ValueError:
                print('Enter an integer between {} and {}.'.format(SPEED_MIN, SPEED_MAX))
                continue
            if value is None:
                break
            with target_lock:
                target_speed = value
            print('Ramping to', value)
    except KeyboardInterrupt:
        print("Keyboard interrupt...")
    except Exception as e:
        print("Error: " + str(e))
    finally:
        stop.set()
        try:
            hover_serial.send_command(0, 0)
        except Exception:
            pass
        hover_serial.close()
