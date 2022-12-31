
from PyQt5.QtWidgets import \
    QApplication as Application, \
    QWidget as Widget, \
    QLabel as Label, \
    QComboBox as ComboBox, \
    QSlider as Slider, \
    QPushButton as Button, \
    QHBoxLayout as HBoxLayout, \
    QVBoxLayout as VBoxLayout

from PyQt5.QtCore import \
    pyqtSignal as signal, \
    QThread as Thread, \
    Qt

from subprocess import \
    Popen, PIPE, STDOUT

from re import \
    fullmatch

from time import \
    sleep

from functools import \
    partial

import os, sys


class ReadLoop(Thread):

    sig_receive = signal(str)

    def __init__(self, handle, receive_func):
        super().__init__()
        self.sig_receive.connect(receive_func)
        self.handle = handle
        self.start()

    def run(self):
        while 1:
            new_data = self.handle.read1().decode('ascii')
            if not new_data:
                break
            self.sig_receive.emit(new_data)
        print('\n[ReadLoop DONE]\n')


class Process(Thread):

    sig_receive = signal(str)
    sig_stop = signal(int)

    def __init__(self, cmd, receive_func, stop_func):
        super().__init__()
        self.sig_stop.connect(stop_func)
        self.sig_receive.connect(receive_func)
        self.proc = Popen(cmd, shell=False, stdin=PIPE, stdout=PIPE, stderr=STDOUT)
        self.readloop = ReadLoop(self.proc.stdout, self.receive)
        self.sendbuf = self.recvbuf = ''
        self.start()

    def run(self):
        while 1:
            retcode = self.proc.poll()
            if retcode is not None:
                self.sig_receive.disconnect()
                self.sig_stop.emit(retcode)
                break
            sleep(0.0333)

    def send(self, data):
        self.sendbuf += data + '\n'
        if '\n' in self.sendbuf:
            readydata, self.sendbuf = self.sendbuf.rsplit('\n', 1)
            readydata += '\n'
            self.proc.stdin.write(bytes(readydata, encoding='ascii'))
            try: self.proc.stdin.flush()
            except: pass

    def receive(self, data):
        self.recvbuf += data
        if '\n' in self.recvbuf:
            parts = self.recvbuf.split('\n')
            self.recvbuf = parts[-1]
            for line in parts[:-1]:
                self.sig_receive.emit(line.strip('\r'))

    def stop(self):
        self.proc.kill()


class MainWindow(Widget):

    cmd = "main.exe"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.setWindowTitle("Paco")
        self.setWindowFlags(Qt.Window | Qt.WindowStaysOnTopHint)
        self.setFixedSize(900, 300)

        def box(typ, content):
            widget = Widget()
            layout = BoxLayout(0 if typ == 'left' else 2, widget)
            for w in content:
                layout.addWidget(w)
            return widget
        
        def Left(*content):
            return box('left', content)
        
        def Top(*content):
            return box('top', content)

        self.label1 = Label('From:', objectName='label1')
        self.label2 = Label('To:', objectName='label2')
        self.combo1 = ComboBox(objectName='combo1')
        self.combo2 = ComboBox(objectName='combo2')
        self.button = Button('Play', objectName='button')

        self.label1.setFixedWidth(50)
        self.label2.setFixedWidth(50)        
        self.button.setFixedSize(100, 100)

        self.ui = \
          Top(
              Left(
                  Top(
                      Left( self.label1, self.combo1 ),
                      Left( self.label2, self.combo2 )
                  ),
                  self.button
              )
          )

        self.setLayout(self.ui.layout())
        self.setStyleSheet('')

        self.button.clicked.connect(self.on_play_clicked)

        self.proc = Process(self.cmd, self.on_receive, self.on_stop)
        
        self.routes = []
        
    def add_route_row(self, sd, dd):
        vbox = VBoxLayout()
        vbox.addWidget(Label('Route %s -> %s' % (sd, dd)))
        hbox = HBoxLayout()
        hbox.addWidget(Label('Delay:'))
        slider = Slider(Qt.Horizontal)
        slider.setRange(0, 10000)
        hbox.addWidget(slider)
        vbox.addLayout(hbox)
        slider.valueChanged.connect(partial(self.set_route_delay, sd, dd))
        self.layout().addLayout(vbox)
    
    def set_route_delay(self, sd, dd, val):
        cmd = '%s 0 %s 0 %s\n%s 1 %s 1 %s' % (sd, dd, val, sd, dd, val)
        self.proc.send(cmd)

    def on_play_clicked(self, e):
        sd = fullmatch("\s+([0-9]+)\s+.*", self.combo1.currentText()).groups()[0]
        dd = fullmatch("\s+([0-9]+)\s+.*", self.combo2.currentText()).groups()[0]
        self.routes.append((sd, dd))
        cmd = '%s 0 %s 0\n%s 1 %s 1' % (sd, dd, sd, dd)
        self.proc.send(cmd)
        self.add_route_row(sd, dd)

    def closeEvent(self, e):
        self.proc.stop()

    def on_receive(self, data):
        print(data)
        match = fullmatch("\s+([0-9]+)\s+([0-9]+|-)\s([0-9]+|-)\s+\"(.*)\"\s\"(.*)\".*", data)
        if match:
            g = match.groups()
            name = "   %s[%s]   %s" % (g[0].ljust(5), g[3], g[4])
            if g[1] != '-': self.combo1.addItem(name)
            if g[2] != '-': self.combo2.addItem(name)

    def on_stop(self, retcode):
        os.exit(retcode)


if __name__ == '__main__':

    app = Application([])

    win = MainWindow()
    win.show()

    stdin = ReadLoop(sys.stdin.buffer, win.proc.send)

    try:
        app.exec()
    except:
        print('ERR\n')
        sleep(5)

    sys.stdin.buffer.raw.close()
