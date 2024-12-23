const SystemMouseWatcher = require('electron-mouse-watcher');

interface MouseWatcherEvent {
    button: 'left' | 'right';
    x: number;
    y: number;
}


class MouseWatcher {
    watcher: any

    private eventHandler(event: MouseWatcherEvent,) {
        if (event.button === 'left') {
            //...
        } else if (event.button === 'right') {
            //...
        }
    }
    startListening() {
        this.watcher = new SystemMouseWatcher((event) => this.eventHandler(event))
    }
    restartListening() {
        this.watcher.restartListening((event) => this.eventHandler(event))
    }
    stopListening() {
        this.watcher.stopListening()
    }
}


export default MouseWatcher