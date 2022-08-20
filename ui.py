

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

import os

from re import fullmatch

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
        
        self.setWindowTitle(self.cmd)
        self.setFixedSize(800, 600)
        self.setLayout(VBoxLayout())
        hbox = HBoxLayout()
        vbox2 = VBoxLayout()
        self.combo1 = ComboBox()
        self.combo2 = ComboBox()
        vbox2.addWidget(self.combo1)
        vbox2.addWidget(self.combo2)
        hbox.addLayout(vbox2)
        self.button = Button("play")
        self.button.setFixedWidth(50)
        hbox.addWidget(self.button)
        self.layout().addLayout(hbox)
        self.label = TextEdit("Starting ...")
        self.label.setReadOnly(True)
        self.layout().addWidget(self.label)
        self.lineedit = LineEdit()
        self.layout().addWidget(self.lineedit)
        
        self.lineedit.returnPressed.connect(self.on_lineedit_send)
        self.button.clicked.connect(self.on_play_clicked)

        self.proc = Process(self.cmd, self.on_start, self.on_stop, self.on_receive)
        self.recvbuf = ''

    def on_play_clicked(self, e):
        sd = self.combo1.currentText().split(' ', 1)[0]
        dd = self.combo2.currentText().split(' ', 1)[0]
        cmd = '%s 0 %s 0\n%s 1 %s 1\n' % (sd, dd, sd, dd)
        self.label.append(cmd)
        self.proc.send(cmd)

    def on_lineedit_send(self):
        cmd = self.lineedit.text() +'\n'
        self.label.append(cmd)
        self.proc.send(cmd)
    
    def closeEvent(self, e):
        self.proc.proc.kill()

    def on_start(self):
        self.label.clear()

    def on_receive(self, data):
        self.label.append(data)
        self.recvbuf += data
        if '\n' in self.recvbuf:
            parts = self.recvbuf.split('\n')
            self.recvbuf = parts[-1]
            for line in parts[:-1]:
                line = line.strip('\r')
                match = fullmatch( "\s+([0-9]+)\s+([0-9]+|-)\s([0-9]+|-)\s+\"(.*)\"\s\"(.*)\".*", line )
                if match:
                    g = match.groups()
                    name = "%s %s %s" % (g[0], g[3], g[4])
                    if g[1] != '-':
                        self.combo1.addItem(name)
                    if g[2] != '-':
                        self.combo2.addItem(name)

    def on_stop(self, retcode):
        os._exit(retcode)


if __name__ == '__main__':

    app = Application([])
    win = MainWindow()
    win.show()
    app.exec()
