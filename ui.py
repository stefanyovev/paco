
from PyQt5.QtWidgets import \
    QApplication as Application, \
    QWidget as Widget, \
    QLabel as Label, \
    QComboBox as ComboBox, \
    QSlider as Slider, \
    QPushButton as Button, \
    QBoxLayout as BoxLayout

from PyQt5.QtCore import \
    pyqtSignal as signal, \
    QThread as Thread, \
    QObject as Object, \
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


def box(typ, content):
    widget = Widget()
    layout = BoxLayout(0 if typ == 'left' else 2, widget)
    layout.setContentsMargins(0, 0, 0, 0)
    if content and type(content[0]) is str:
        widget.setObjectName(content[0])
        content = content[1:]
    for w in content:
        layout.addWidget(w)
    return widget

def Left(*content):
    return box('left', content)

def Top(*content):
    return box('top', content)


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


class GUIShellApp(Object):
    """ To be Extended.
        __init__ assumes class member 'cmd'.
        __init__ assumes instance member 'ui'. """
        
    sig_core_output = signal(str)
    sig_core_stopped = signal(int)

    def __init__(self):
        super().__init__()
    
        self.core = Process(
            self.cmd,
            self.sig_core_output,
            self.sig_core_stopped)
        
        self.ui.setWindowFlags(Qt.Window | Qt.WindowStaysOnTopHint)
        self.ui.setWindowTitle(self.cmd+' UI')
        self.ui.setObjectName('root')
        self.ui.closeEvent = self.ui_close_event        
        self.ui.show()

    def stop(self):
        self.core.stop()

    def ui_close_event(self, e):
        self.stop()

    def exec(self):
        return self.app.exec()


# ----------------------------------------------------------------------------


class PACO_GUI(GUIShellApp):

    cmd = "main.exe"
    
    def __init__(self):

        self.app = Application([])
        
        self.label1 = Label('From:', objectName='label1')
        self.label2 = Label('To:', objectName='label2')
        self.combo1 = ComboBox(objectName='combo1')
        self.combo2 = ComboBox(objectName='combo2')
        self.button = Button('Play', objectName='button')

        self.ui = \
          Top(
              Left('head',
                  Top(
                      Left( self.label1, self.combo1 ),
                      Left( self.label2, self.combo2 )
                  ),
                  self.button
              )
          )

        self.ui.setStyleSheet("""
          #root {
            min-width: 1900px; max-width: 1900px;
            min-height: 600px; max-height: 600px; }
          #head {
            max-height: 150px;
            background-color: gray; }
          #label1, #label2 {
            min-width: 100px; max-width: 100px; }
          #button {
            min-width: 100px; max-width: 100px;
            min-height: 100px; max-height: 100px; }
          """)

        super().__init__()
        self.button.clicked.connect(self.on_play_clicked)
        self.sig_core_output.connect(self.on_receive)
        self.sig_core_stopped.connect(exit)
        self.routes = []
        self.rows = []
          
    def add_route_row(self, sd, dd):
        label1 = Label('Route %s -> %s' % (sd, dd))
        label2 = Label('Delay:')
        slider = Slider(Qt.Horizontal)
        slider.setRange(0, 10000)
        slider.valueChanged.connect(partial(self.set_route_delay, sd, dd))
        row = Top(label1, Left(label2, slider))
        row.setStyleSheet(""" max-height: 100px; """)
        self.rows.append(row)
        self.ui.layout().addWidget(row)
    
    def set_route_delay(self, sd, dd, val):
        cmd = '%s 0 %s 0 %s\n%s 1 %s 1 %s' % (sd, dd, val, sd, dd, val)
        self.core.send(cmd)

    def on_play_clicked(self, e):
        sd = fullmatch("\s+([0-9]+)\s+.*", self.combo1.currentText()).groups()[0]
        dd = fullmatch("\s+([0-9]+)\s+.*", self.combo2.currentText()).groups()[0]
        self.routes.append((sd, dd))
        cmd = '%s 0 %s 0\n%s 1 %s 1' % (sd, dd, sd, dd)
        self.core.send(cmd)
        self.add_route_row(sd, dd)

    def on_receive(self, data):
        print(data)
        match = fullmatch("\s+([0-9]+)\s+([0-9]+|-)\s([0-9]+|-)\s+\"(.*)\"\s\"(.*)\".*", data)
        if match:
            g = match.groups()
            name = "   %s[%s]   %s" % (g[0].ljust(5), g[3], g[4])
            if g[1] != '-': self.combo1.addItem(name)
            if g[2] != '-': self.combo2.addItem(name)


if __name__ == '__main__':

    app = PACO_GUI()

    stdin = ReadLoop(sys.stdin.buffer, app.core.send)

    try:
        app.exec()
    except:
        print('ERR\n')
        sleep(5)

    sys.stdin.buffer.raw.close()
