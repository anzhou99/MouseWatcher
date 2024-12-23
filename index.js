const {
  startListening,
  stopListening,
  restartListening,
} = require("./build/Release/mouse_watcher.node");

class SystemMouseWatcher {
  listener;
  constructor(callback) {
    // 启动监听并保存监听器实例
    this.listener = startListening((event) => {
      // 转发事件到用户提供的回调
      callback(event);
    });
  }

  stopListening() {
    // 停止监听
    stopListening(this.listener);
  }

  restartListening(newCallback) {
    // 重新启动监听，使用新的回调函数
    return restartListening(this.listener, (event) => {
      // 转发事件到新的回调函数
      newCallback(event);
    });
  }
}

module.exports = SystemMouseWatcher;
