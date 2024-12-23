#include <napi.h>
#include <Windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <chrono>

class MouseListener {
private:
    Napi::ThreadSafeFunction tsfn;// Node.js的线程安全函数包装器
    std::thread hookThread; // 监听鼠标事件的线程
    std::atomic<bool> isRunning{false}; // 原子布尔值，用于控制线程运行状态
    HHOOK mouseHook;// Windows系统鼠标钩子句柄
    HANDLE hStopEvent;
    std::mutex callbackMutex;
    std::atomic<bool> isCallbackAlive{true};

    // 静态成员，用于在全局钩子回调中访问实例
    static MouseListener* currentInstance;

    // 全局鼠标钩子回调函数
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode >= 0 && currentInstance) {
            // 只处理鼠标按键按下事件
            if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
                MSLLHOOKSTRUCT* pMouseStruct = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
                
                currentInstance->NotifyMouseClick(
                    wParam == WM_LBUTTONDOWN ? "left" : "right", 
                    pMouseStruct->pt.x, 
                    pMouseStruct->pt.y
                );
            }
        }
        
        // 传递给下一个钩子
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    void NotifyMouseClick(const std::string& button, int x, int y) {
        // 如果回调已经失效，则不再尝试发送
        if (!isCallbackAlive.load()) return;

        // 使用线程安全函数发送事件
        napi_status status = tsfn.BlockingCall([this, button, x, y](Napi::Env env, Napi::Function callback) {
            if (callback) {
                try {
                    Napi::Object eventObj = Napi::Object::New(env);
                    eventObj.Set("button", Napi::String::New(env, button));
                    eventObj.Set("x", Napi::Number::New(env, x));
                    eventObj.Set("y", Napi::Number::New(env, y));
                    
                    callback.Call({eventObj});
                } catch (const std::exception& e) {
                    std::cerr << "Callback Error: " << e.what() << std::endl;
                    // 标记回调已失效
                    isCallbackAlive.store(false);
                }
            }
        });

        // 如果发送失败，标记回调已失效
        if (status != napi_ok) {
            std::cerr << "Failed to send mouse event" << std::endl;
            isCallbackAlive.store(false);
        }
    }

    void RunHookThread() {
        // 创建消息循环线程
        DWORD threadId = GetCurrentThreadId();
        
        // 设置全局钩子
        mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
        
        if (!mouseHook) {
            std::cerr << "Failed to set mouse hook" << std::endl;
            return;
        }

        // 消息循环
        MSG msg;
        while (isRunning.load()) {
            // 使用 PeekMessage 替代 GetMessage，避免阻塞
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // 检查是否需要停止
            if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
                break;
            }

            // 防止 CPU 过度占用
            Sleep(10);

            // 定期检查回调是否仍然有效
            if (!isCallbackAlive.load()) {
                std::cerr << "Callback became invalid. Stopping hook thread." << std::endl;
                break;
            }
        }

        // 清理钩子
        if (mouseHook) {
            UnhookWindowsHookEx(mouseHook);
            mouseHook = NULL;
        }
    }

public:
    MouseListener(Napi::Env env, Napi::Function callback) {
        // 创建停止事件
        hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        // 设置当前实例
        currentInstance = this;

        // 创建线程安全函数 - 使用更严格的配置
        tsfn = Napi::ThreadSafeFunction::New(
            env,
            callback,
            "MouseListener",
            0,  // 无限制队列
            1,  // 1个线程
            [this](Napi::Env) {
                // 清理时标记回调失效
                isCallbackAlive.store(false);
                Stop();
            }
        );

        // 启动监听
        isRunning.store(true);
        isCallbackAlive.store(true);
        hookThread = std::thread(&MouseListener::RunHookThread, this);
    }

    ~MouseListener() {
        Stop();
    }

    void Stop() {
        if (isRunning.load()) {
            // 标记停止
            isRunning.store(false);
            isCallbackAlive.store(false);

            // 触发停止事件
            if (hStopEvent) {
                SetEvent(hStopEvent);
            }

            // 等待线程结束
            if (hookThread.joinable()) {
                hookThread.join();
            }

            // 关闭事件句柄
            if (hStopEvent) {
                CloseHandle(hStopEvent);
                hStopEvent = NULL;
            }

            // 释放线程安全函数
            tsfn.Release();

            // 清除当前实例
            if (currentInstance == this) {
                currentInstance = nullptr;
            }
        }
    }

    // 重新启动监听
    bool Restart(Napi::Env env, Napi::Function callback) {
        // 先停止当前监听
        Stop();

        // 重新初始化
        hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        currentInstance = this;

        // 重新创建线程安全函数
        tsfn = Napi::ThreadSafeFunction::New(
            env,
            callback,
            "MouseListener",
            0,  // 无限制队列
            1,  // 1个线程
            [this](Napi::Env) {
                isCallbackAlive.store(false);
                Stop();
            }
        );

        // 重新启动监听
        isRunning.store(true);
        isCallbackAlive.store(true);
        hookThread = std::thread(&MouseListener::RunHookThread, this);

        return true;
    }
};

// 初始化静态成员
MouseListener* MouseListener::currentInstance = nullptr;

// 创建鼠标监听器的函数
Napi::Value StartMouseListening(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 检查是否提供了回调函数
    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Callback function is required").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 获取回调函数
    Napi::Function callback = info[0].As<Napi::Function>();

    // 创建 MouseListener 对象并将其包装为外部引用
    auto* listener = new MouseListener(env, callback);
    return Napi::External<MouseListener>::New(env, listener, 
        [](Napi::Env env, MouseListener* listener) {
            delete listener;
        }
    );
}

// 停止监听的函数
Napi::Value StopMouseListening(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 检查参数是否为之前创建的监听器
    if (info.Length() < 1 || !info[0].IsExternal()) {
        Napi::TypeError::New(env, "Invalid listener").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 获取并停止监听器
    auto listener = info[0].As<Napi::External<MouseListener>>().Data();
    listener->Stop();

    return env.Null();
}

// 重新启动监听的函数
Napi::Value RestartMouseListening(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 检查参数是否正确
    if (info.Length() < 2 || 
        !info[0].IsExternal() || 
        !info[1].IsFunction()) {
        Napi::TypeError::New(env, "Invalid arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 获取监听器和新的回调函数
    auto listener = info[0].As<Napi::External<MouseListener>>().Data();
    Napi::Function callback = info[1].As<Napi::Function>();

    // 重新启动监听
    bool result = listener->Restart(env, callback);
    return Napi::Boolean::New(env, result);
}

// 定义 N-API 模块
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("startListening", Napi::Function::New(env, StartMouseListening));
    exports.Set("stopListening", Napi::Function::New(env, StopMouseListening));
    exports.Set("restartListening", Napi::Function::New(env, RestartMouseListening));
    return exports;
}

// 定义模块
NODE_API_MODULE(mouse_listener, Init)