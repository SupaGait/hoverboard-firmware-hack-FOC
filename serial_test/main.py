from HoverSerial import *
import threading
import time

stop = threading.Event()


def thread_send_command():
    SPEED_MAX_TEST = 60  # [-] Maximum speed for testing
    SPEED_STEP = 2  # [-] Speed step
    TIME_SEND = 0.3  # [s] Sending time interval

    iStep = SPEED_STEP
    iTest = 0
    steer = 0

    while not stop.is_set():
        speed = SPEED_MAX_TEST - 2 * abs(iTest)
        hover_serial.send_command(steer, speed)
        print('Sending:\t steer: ' + str(steer) + ' speed: ' + str(speed))

        iTest += iStep
        if iTest >= SPEED_MAX_TEST or iTest <= -SPEED_MAX_TEST:
            iStep = -iStep

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


if __name__ == "__main__":
    SERIAL_PORT = 'COM12'
    SERIAL_BAUD = 115200
    hover_serial = Hoverboard_serial(SERIAL_PORT, SERIAL_BAUD)

    thread1 = threading.Thread(target=thread_send_command, daemon=True)
    thread2 = threading.Thread(target=thread_receive_feedback, daemon=True)
    thread1.start()
    thread2.start()

    try:
        while thread1.is_alive() or thread2.is_alive():
            time.sleep(0.2)
    except KeyboardInterrupt:
        print("Keyboard interrupt...")
        stop.set()
        try:
            hover_serial.send_command(0, 0)
        except Exception:
            pass
    except Exception as e:
        print("Error: " + str(e))
        stop.set()
    finally:
        stop.set()
        hover_serial.close()
