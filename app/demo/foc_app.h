#pragma once

namespace foc_app {

// 启动 FOC 线程 (main 中调用)
int start();

// 供 UART RX 回调调用
void feed_char(char c);

} // namespace foc_app
