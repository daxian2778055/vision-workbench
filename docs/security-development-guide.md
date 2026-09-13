# VisionFlowPlatform 安全开发指南

## 概述

本指南提供了 VisionFlowPlatform 项目的安全开发最佳实践，帮助开发者识别和防范安全风险。

---

## 1. 安全原则

### 1.1 最小权限原则
- 只授予完成任务所需的最小权限
- 避免使用管理员权限运行程序
- 限制网络访问权限

### 1.2 纵深防御
- 多层安全防护
- 不依赖单一安全措施
- 定期安全审计

### 1.3 安全默认设置
- 默认配置应该是安全的
- 用户需要明确启用不安全功能
- 提供安全配置选项

---

## 2. 输入验证

### 2.1 验证所有输入

#### 字符串输入
```cpp
bool validateStringInput(const QString &input, int maxLength = 1000) {
    // 检查长度
    if (input.length() > maxLength) {
        qWarning() << "Input too long:" << input.length();
        return false;
    }
    
    // 检查特殊字符
    static QRegularExpression invalidChars("[<>\"'&;]");
    if (invalidChars.match(input).hasMatch()) {
        qWarning() << "Invalid characters in input";
        return false;
    }
    
    return true;
}
```

#### 数值输入
```cpp
bool validateNumericInput(int value, int min, int max) {
    if (value < min || value > max) {
        qWarning() << "Value out of range:" << value;
        return false;
    }
    return true;
}
```

#### 文件路径
```cpp
bool validateFilePath(const QString &path) {
    // 检查路径遍历攻击
    if (path.contains("..") || path.contains("~")) {
        qWarning() << "Path traversal detected";
        return false;
    }
    
    // 检查特殊字符
    static QRegularExpression invalidChars("[<>:\"|?*]");
    if (invalidChars.match(path).hasMatch()) {
        qWarning() << "Invalid characters in path";
        return false;
    }
    
    // 检查路径长度
    if (path.length() > MAX_PATH) {
        qWarning() << "Path too long";
        return false;
    }
    
    return true;
}
```

### 2.2 参数化查询

#### SQL 查询
```cpp
// 好：使用参数化查询
QSqlQuery query;
query.prepare("SELECT * FROM users WHERE username = ?");
query.addBindValue(username);
query.exec();

// 不好：字符串拼接
QString sql = QString("SELECT * FROM users WHERE username = '%1'").arg(username);
QSqlQuery query(sql);
```

### 2.3 数据验证

#### JSON 数据
```cpp
bool validateJson(const QJsonObject &json) {
    // 检查必需字段
    QStringList requiredFields = {"name", "type", "value"};
    for (const QString &field : requiredFields) {
        if (!json.contains(field)) {
            qWarning() << "Missing required field:" << field;
            return false;
        }
    }
    
    // 检查字段类型
    if (!json["name"].isString()) {
        qWarning() << "Invalid type for 'name'";
        return false;
    }
    
    return true;
}
```

---

## 3. 内存安全

### 3.1 避免缓冲区溢出

#### 使用安全函数
```cpp
// 好：使用 QString
QString str = userInput;

// 不好：使用 char*
char buffer[100];
strcpy(buffer, userInput);  // 危险！

// 好：使用安全版本
char buffer[100];
strncpy(buffer, userInput, sizeof(buffer) - 1);
buffer[sizeof(buffer) - 1] = '\0';
```

### 3.2 智能指针

#### 避免悬空指针
```cpp
// 好：使用智能指针
auto data = std::make_unique<DataObject>();

// 不好：裸指针
DataObject *data = new DataObject();
// 可能忘记 delete
```

### 3.3 边界检查

#### 数组访问
```cpp
// 好：检查边界
if (index >= 0 && index < array.size()) {
    int value = array[index];
}

// 不好：不检查边界
int value = array[index];  // 可能越界
```

---

## 4. 线程安全

### 4.1 互斥锁

#### 保护共享数据
```cpp
class ThreadSafeCounter {
public:
    void increment() {
        QMutexLocker locker(&m_mutex);
        m_count++;
    }
    
    int value() const {
        QMutexLocker locker(&m_mutex);
        return m_count;
    }
    
private:
    mutable QMutex m_mutex;
    int m_count = 0;
};
```

### 4.2 原子操作

#### 使用原子变量
```cpp
class AtomicCounter {
public:
    void increment() {
        m_count.fetchAndAddRelaxed(1);
    }
    
    int value() const {
        return m_count.loadAcquire();
    }
    
private:
    QAtomicInt m_count;
};
```

### 4.3 信号槽安全

#### 跨线程连接
```cpp
// 好：使用 QueuedConnection
connect(sender, &Sender::signal,
        receiver, &Receiver::slot,
        Qt::QueuedConnection);

// 不好：直接调用（可能跨线程）
receiver->slot();
```

---

## 5. 网络安全

### 5.1 数据加密

#### 传输加密
```cpp
// 使用 TLS/SSL
QSslSocket *socket = new QSslSocket();
socket->connectToHostEncrypted("example.com", 443);

if (socket->waitForEncrypted()) {
    socket->write("secure data");
}
```

#### 存储加密
```cpp
// 加密敏感数据
QByteArray encryptData(const QByteArray &data, const QByteArray &key) {
    // 使用 AES 加密
    // ...
}

// 解密数据
QByteArray decryptData(const QByteArray &encrypted, const QByteArray &key) {
    // 使用 AES 解密
    // ...
}
```

### 5.2 认证和授权

#### 用户认证
```cpp
bool authenticateUser(const QString &username, const QString &password) {
    // 验证用户名和密码
    QSqlQuery query;
    query.prepare("SELECT password_hash FROM users WHERE username = ?");
    query.addBindValue(username);
    query.exec();
    
    if (query.next()) {
        QString storedHash = query.value(0).toString();
        return verifyPassword(password, storedHash);
    }
    
    return false;
}
```

#### 权限检查
```cpp
bool checkPermission(const QString &user, const QString &resource, const QString &action) {
    // 检查用户是否有权限执行操作
    QSqlQuery query;
    query.prepare("SELECT COUNT(*) FROM permissions WHERE user = ? AND resource = ? AND action = ?");
    query.addBindValue(user);
    query.addBindValue(resource);
    query.addBindValue(action);
    query.exec();
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}
```

### 5.3 防止常见攻击

#### SQL 注入防护
```cpp
// 使用参数化查询
QSqlQuery query;
query.prepare("INSERT INTO logs (message, timestamp) VALUES (?, ?)");
query.addBindValue(message);
query.addBindValue(QDateTime::currentDateTime());
query.exec();
```

#### XSS 防护
```cpp
// 转义 HTML 字符
QString escapeHtml(const QString &input) {
    QString output = input;
    output.replace("&", "&amp;");
    output.replace("<", "&lt;");
    output.replace(">", "&gt;");
    output.replace("\"", "&quot;");
    output.replace("'", "&#x27;");
    return output;
}
```

---

## 6. 文件安全

### 6.1 文件权限

#### 检查文件权限
```cpp
bool checkFilePermissions(const QString &path) {
    QFileInfo info(path);
    
    // 检查文件是否存在
    if (!info.exists()) {
        return false;
    }
    
    // 检查文件权限
    if (!info.isReadable()) {
        qWarning() << "File not readable:" << path;
        return false;
    }
    
    if (!info.isWritable()) {
        qWarning() << "File not writable:" << path;
        return false;
    }
    
    return true;
}
```

### 6.2 安全文件操作

#### 临时文件
```cpp
// 创建安全的临时文件
QTemporaryFile tempFile;
if (tempFile.open()) {
    // 使用临时文件
    tempFile.write(data);
    tempFile.close();
}
// 临时文件自动删除
```

#### 文件锁定
```cpp
// 使用文件锁
QFile file("data.txt");
if (file.open(QIODevice::ReadWrite)) {
    if (file.lock(QFile::ReadWrite)) {
        // 安全读写文件
        file.write(data);
        file.unlock();
    }
    file.close();
}
```

---

## 7. 日志和审计

### 7.1 安全日志

#### 记录安全事件
```cpp
void logSecurityEvent(const QString &event, const QString &user, const QString &details) {
    QString logEntry = QString("%1 | %2 | %3 | %4")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"))
        .arg(event)
        .arg(user)
        .arg(details);
    
    // 写入安全日志文件
    QFile logFile("security.log");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&logFile);
        stream << logEntry << "\n";
        logFile.close();
    }
    
    // 输出到调试控制台
    qCWarning(vfpCat) << "Security event:" << logEntry;
}
```

### 7.2 审计跟踪

#### 记录操作日志
```cpp
void auditLog(const QString &user, const QString &action, const QString &resource) {
    QSqlQuery query;
    query.prepare("INSERT INTO audit_log (timestamp, user, action, resource) VALUES (?, ?, ?, ?)");
    query.addBindValue(QDateTime::currentDateTime());
    query.addBindValue(user);
    query.addBindValue(action);
    query.addBindValue(resource);
    query.exec();
}
```

---

## 8. 密码安全

### 8.1 密码哈希

#### 使用安全的哈希算法
```cpp
QString hashPassword(const QString &password) {
    // 使用 bcrypt 或 Argon2
    QByteArray salt = generateSalt();
    QByteArray hash = bcrypt(password.toUtf8(), salt);
    return QString(hash.toBase64());
}

bool verifyPassword(const QString &password, const QString &storedHash) {
    QByteArray hash = QByteArray::fromBase64(storedHash.toUtf8());
    return bcryptVerify(password.toUtf8(), hash);
}
```

### 8.2 密码策略

#### 验证密码强度
```cpp
bool validatePasswordStrength(const QString &password) {
    // 检查长度
    if (password.length() < 8) {
        return false;
    }
    
    // 检查复杂度
    bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;
    
    for (const QChar &ch : password) {
        if (ch.isUpper()) hasUpper = true;
        else if (ch.isLower()) hasLower = true;
        else if (ch.isDigit()) hasDigit = true;
        else hasSpecial = true;
    }
    
    return hasUpper && hasLower && hasDigit && hasSpecial;
}
```

---

## 9. 错误处理

### 9.1 安全的错误消息

#### 避免信息泄露
```cpp
// 好：通用错误消息
void handleError() {
    qWarning() << "An error occurred";
    showError("An error occurred. Please try again.");
}

// 不好：详细错误消息（可能泄露信息）
void handleError(const QString &details) {
    qWarning() << "Error:" << details;
    showError("Error: " + details);  // 可能包含敏感信息
}
```

### 9.2 异常安全

#### 捕获所有异常
```cpp
try {
    // 可能抛出异常的代码
    riskyOperation();
} catch (const std::exception &e) {
    qWarning() << "Exception:" << e.what();
    // 记录日志但不泄露详细信息
    logError("An error occurred");
} catch (...) {
    qWarning() << "Unknown exception";
    logError("An unknown error occurred");
}
```

---

## 10. 安全配置

### 10.1 环境变量

#### 使用环境变量存储敏感信息
```cpp
// 读取环境变量
QString apiKey = qEnvironmentVariable("API_KEY");
QString dbPassword = qEnvironmentVariable("DB_PASSWORD");

// 检查环境变量是否存在
if (apiKey.isEmpty()) {
    qWarning() << "API_KEY not set";
    return false;
}
```

### 10.2 配置文件安全

#### 保护配置文件
```cpp
// 设置配置文件权限
QFile configFile("config.ini");
if (configFile.exists()) {
    QFile::setPermissions(configFile.fileName(), 
                          QFile::ReadOwner | QFile::WriteOwner);
}
```

---

## 11. 依赖安全

### 11.1 依赖审计

#### 检查依赖漏洞
```bash
# 使用 OWASP Dependency-Check
dependency-check --project "VisionFlowPlatform" --scan .

# 使用 Snyk
snyk test
```

### 11.2 依赖更新

#### 定期更新依赖
```bash
# 更新 Qt
# 更新 OpenCV
# 更新 HALCON
# 更新其他依赖
```

---

## 12. 安全测试

### 12.1 静态分析

#### 使用安全扫描工具
```bash
# 使用 cppcheck
cppcheck --enable=all --security src/

# 使用 clang-tidy
clang-tidy -p build src/*.cpp --checks='*,-llvm-header-guard'
```

### 12.2 动态分析

#### 使用内存检测工具
```bash
# Windows: Application Verifier
# Linux: Valgrind
valgrind --tool=memcheck ./build/bin/VisionFlowPlatform
```

### 12.3 模糊测试

#### 输入模糊测试
```cpp
void fuzzTest(const QByteArray &fuzzData) {
    // 解析模糊数据
    QJsonDocument doc = QJsonDocument::fromJson(fuzzData);
    
    if (!doc.isNull()) {
        // 处理解析结果
        processJson(doc.object());
    }
}
```

---

## 13. 安全编码检查清单

### 13.1 输入验证
- [ ] 验证所有用户输入
- [ ] 检查输入长度
- [ ] 过滤特殊字符
- [ ] 使用参数化查询

### 13.2 内存安全
- [ ] 使用智能指针
- [ ] 检查数组边界
- [ ] 避免缓冲区溢出
- [ ] 及时释放资源

### 13.3 线程安全
- [ ] 保护共享数据
- [ ] 使用原子操作
- [ ] 正确使用信号槽
- [ ] 避免死锁

### 13.4 网络安全
- [ ] 使用加密传输
- [ ] 验证服务器证书
- [ ] 防止注入攻击
- [ ] 实现认证授权

### 13.5 文件安全
- [ ] 检查文件权限
- [ ] 防止路径遍历
- [ ] 使用安全临时文件
- [ ] 验证文件完整性

### 13.6 密码安全
- [ ] 使用强密码策略
- [ ] 安全存储密码
- [ ] 使用安全哈希算法
- [ ] 实现密码重置

### 13.7 错误处理
- [ ] 避免信息泄露
- [ ] 记录安全日志
- [ ] 实现异常处理
- [ ] 提供友好错误消息

### 13.8 配置安全
- [ ] 使用环境变量
- [ ] 保护配置文件
- [ ] 定期更新依赖
- [ ] 进行安全审计

---

## 14. 安全事件响应

### 14.1 事件分类

#### 严重程度
- **紧急**: 系统被入侵、数据泄露
- **高**: 发现严重漏洞、权限提升
- **中**: 发现中等漏洞、配置问题
- **低**: 发现低风险问题、最佳实践建议

### 14.2 响应流程

#### 步骤 1：识别
- 发现安全事件
- 确定事件类型
- 评估严重程度

#### 步骤 2：遏制
- 隔离受影响系统
- 防止事件扩大
- 保存证据

#### 步骤 3：消除
- 修复漏洞
- 清除威胁
- 恢复系统

#### 步骤 4：恢复
- 恢复服务
- 验证修复
- 监控系统

#### 步骤 5：总结
- 分析原因
- 记录教训
- 改进流程

---

## 15. 安全培训

### 15.1 开发者培训

#### 培训内容
- 安全编码实践
- 常见漏洞类型
- 安全测试方法
- 安全工具使用

#### 培训频率
- 新员工入职培训
- 定期安全培训
- 专项安全培训

### 15.2 安全意识

#### 最佳实践
- 遵循安全编码规范
- 定期进行安全审计
- 及时更新依赖
- 关注安全公告

---

## 16. 总结

通过本指南，您可以：

1. **理解安全原则**: 最小权限、纵深防御、安全默认
2. **实现输入验证**: 防止注入攻击、缓冲区溢出
3. **确保内存安全**: 智能指针、边界检查
4. **保护线程安全**: 互斥锁、原子操作
5. **加强网络安全**: 加密传输、认证授权
6. **维护文件安全**: 权限检查、路径验证
7. **实施安全日志**: 审计跟踪、事件记录
8. **进行安全测试**: 静态分析、动态分析

安全是一个持续的过程，需要在整个开发生命周期中持续关注和改进。
