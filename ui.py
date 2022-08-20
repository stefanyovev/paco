

from PyQt5.QtWidgets import \
QApplication as Application, \
QWidget as Widget, \
QLabel as Label, \
QComboBox as ComboBox, \
QPushButton as Button, \
QTextEdit as TextEdit, \
QLineEdit as LineEdit, \
QHBoxLayout as HBoxLayout, \
QVBoxLayout as VBoxLayout

from PyQt5.QtCore import \
pyqtSignal as signal, \
QThread as Thread, \
Qt

from PyQt5.QtGui import \
QKeyEvent as KeyEvent

from subprocess import \
Popen, PIPE, STDOUT

from re import \
fullmatch

from time import \
sleep

import os, sys


class ReadLoop(Thread):

    sig_receive = signal(str)

    def __init__(self, buffer):
        super().__init__()
        self.buffer = buffer
        self.start()

    def run(self):
        while 1:
            avail = self.buffer.peek()
            if avail:
                new_data = self.buffer.read(len(avail)).decode('ascii')
                self.sig_receive.emit(new_data)
            sleep(0.01)


class Process(Thread):

    sig_start = signal()
    sig_stop = signal(int)
    sig_receive = signal(str)

    def __init__(self, cmd, start_func, stop_func, receive_func):
        super().__init__()
        self.sig_start.connect(start_func)
        self.sig_stop.connect(stop_func)
        self.sig_receive.connect(receive_func)
        self.proc = Popen(cmd, shell=False, stdin=PIPE, stdout=PIPE, stderr=STDOUT)
        self.started = False
        self.sendbuf = ''        
        self.start()

    def run(self): # READ-LOOP
        while 1:
            retcode = self.proc.poll()
            if retcode is not None:
                self.sig_receive.disconnect()
                self.sig_stop.emit(retcode)
                break
            elif not self.started:
                self.started = True
                self.sig_start.emit()
            avail = self.proc.stdout.peek()
            if avail:
                new_data = self.proc.stdout.read(len(avail)).decode('ascii')
                self.sig_receive.emit(new_data)

    def send(self, data):
            self.sendbuf += data
            if '\n' in self.sendbuf:
                readydata, self.sendbuf = self.sendbuf.rsplit('\n', 1)
                readydata += '\n'
                self.proc.stdin.write(bytes(readydata, encoding='ascii'))
                try: self.proc.stdin.flush()
                except: pass


class MainWindow(Widget):

    cmd = "main.exe"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        
        self.setWindowTitle("Patso player")
        self.setWindowFlags(Qt.Window | Qt.WindowStaysOnTopHint)
        self.setFixedSize(900, 200)
        
        self.setLayout(VBoxLayout())
        self.label1 = Label('From:')
        self.label1.setFixedWidth(50)        
        self.combo1 = ComboBox()
        hbox1 = HBoxLayout()
        hbox1.addWidget(self.label1)
        hbox1.addWidget(self.combo1)
        self.label2 = Label('To:')
        self.label2.setFixedWidth(50)
        self.combo2 = ComboBox()
        hbox2 = HBoxLayout()
        hbox2.addWidget(self.label2)
        hbox2.addWidget(self.combo2)
        vbox = VBoxLayout()
        vbox.addLayout(hbox1)
        vbox.addLayout(hbox2)
        hbox3 = HBoxLayout()
        hbox3.addLayout(vbox)
        self.button = Button("play")
        self.button.setFixedSize(100, 100)
        hbox3.addWidget(self.button)
        self.layout().addLayout(hbox3)
        
        self.button.clicked.connect(self.on_play_clicked)

        self.proc = Process(self.cmd, self.on_start, self.on_stop, self.on_receive)
        self.recvbuf = ''

    def on_play_clicked(self, e):
        sd = fullmatch("\s+([0-9]+)\s+.*", self.combo1.currentText() ).groups()[0]
        dd = fullmatch("\s+([0-9]+)\s+.*", self.combo2.currentText() ).groups()[0]
        cmd = '%s 0 %s 0\n%s 1 %s 1\n' % (sd, dd, sd, dd)
        print(cmd)
        self.proc.send(cmd)

    def closeEvent(self, e):
        self.proc.proc.kill()

    def on_start(self):
        pass

    def on_receive(self, data):
        print(data)
        self.recvbuf += data
        if '\n' in self.recvbuf:
            parts = self.recvbuf.split('\n')
            self.recvbuf = parts[-1]
            for line in parts[:-1]:
                line = line.strip('\r')
                match = fullmatch( "\s+([0-9]+)\s+([0-9]+|-)\s([0-9]+|-)\s+\"(.*)\"\s\"(.*)\".*", line )
                if match:
                    g = match.groups()
                    name = "   %s[%s]   %s" % (g[0].ljust(5), g[3], g[4])
                    if g[1] != '-':
                        self.combo1.addItem(name)
                    if g[2] != '-':
                        self.combo2.addItem(name)

    def on_stop(self, retcode):
        os._exit(retcode)


if __name__ == '__main__':

    app = Application([])
    
    stdin = ReadLoop(sys.stdin.buffer)
    
    win = MainWindow()
    win.show()

    stdin.sig_receive.connect(win.proc.send)
    
    try:
        app.exec()
    except:
        sleep(5)
