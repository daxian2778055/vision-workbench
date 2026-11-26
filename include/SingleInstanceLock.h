#pragma once

class QLockFile;

/// 注册 main() 持有的单实例锁，供语言切换重启路径在 startDetached 前显式释放。
/// 锁本体仍由 main() 的局部变量持有；这里只保存指针，不接管所有权。
void setSingleInstanceLock(QLockFile *lock);

/// 释放单实例锁（幂等）。旧实例还没有从 main() 返回，新进程必须能立即拿到锁，
/// 否则会走"已在运行"分支直接退出（软件只关不开）。
void releaseSingleInstanceLock();
