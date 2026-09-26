# phoenix 本地授权验证

本版本用于在本机验证激活、使用期限、续期、吊销、设备绑定和断网行为。EXE 和 DLL 均启用授权校验，尚未加壳或签名。普通开发版继续不要求授权。

2026-09-15 功能调整：已移除人物移速修改、泰克跨步者配件替换和穿云箭，旧配置中的对应字段会被忽略。离线授权目前仍是方案，本包继续需要下面的本地服务。

本地服务只监听 `127.0.0.1`，不开放到局域网或互联网。卡密数据库、测试签名私钥和生成的公钥头文件放在项目根目录的 `.local-license`，不进入 Git 或客户端压缩包。客户端只包含公钥；会话凭据使用 Windows 当前用户的 DPAPI 加密保存于程序旁的 `.license/session.bin`。

## 两套版本

| 用途 | 构建入口 | 输出 |
| --- | --- | --- |
| 日常开发，无授权限制 | `./build-release.ps1` | `dist/phoenix`、`dist/phoenix.zip` |
| 本地授权验证 | `./build-local-license.ps1` | `dist/phoenix-local-license`、`dist/phoenix-local-license.zip` |

两套版本的程序文件名都为 `phoenix.exe` 和 `phoenix.dll`，但中间文件和生成目录分开，避免误混。不要向客户分发普通开发版；本地授权验证包也不是已经完成服务器部署与保护的正式发行版。

## 快速操作

以下命令在项目根目录的 PowerShell 中运行。需要 Visual Studio C++ 工具链，以及 Python 3.12+ 与 `licensing/server/requirements.txt` 中的依赖。脚本会优先使用可用的本地 Python，也可传 `-Python '完整的python.exe路径'`。本机已有运行环境时无需安装新依赖。

1. 编译本地验证版（自动初始化本地测试密钥，重复运行保留已有卡密和密钥）：

```powershell
./build-local-license.ps1
```

2. 启动本地服务。后台启动成功后可关闭当前 PowerShell 窗口：

```powershell
./licensing/manage-local.ps1 -Action start -LeaseSeconds 10
```

默认端口为 `17891`。这里把短期凭证设为 10 秒，方便测试断网；省略 `-LeaseSeconds` 时为 600 秒。重复启动会复用已有服务；要改变凭证时长，先停止再启动。电脑重启后需要重新启动本地服务。

```powershell
./licensing/manage-local.ps1 -Action status
./licensing/manage-local.ps1 -Action stop
```

仍可用 `-Action serve` 在前台运行；此时必须保留该窗口，按 Ctrl+C 停止。后台服务日志位于 `.local-license/service-17891.stdout.log` 和对应的 `.stderr.log`。

3. 在另一个窗口生成一个有效使用时长为 300 秒的测试激活码：

```powershell
./licensing/manage-local.ps1 -Action issue -Seconds 300
```

结果中的 `activation_code` 是激活码，只在生成时显示；`license_id` 用于续期、吊销等管理操作。使用期限从第一次激活成功时起算，重复激活和换机不会重新起算。

4. 验证激活和运行期到期，无需启动游戏：

```powershell
./dist/phoenix-local-license/phoenix.exe --activate
./dist/phoenix-local-license/phoenix.exe --license-check
./dist/phoenix-local-license/phoenix.exe --license-watch 360
```

激活时在提示处粘贴激活码，输入不回显。最后一条持续验证最多 360 秒；期限内结束返回成功，到期／吊销／无法续发且旧凭证已到期时返回失败。常规双击启动也会在首次使用时要求激活，并在游戏出现后、加载 DLL 前再次验证。

若提示“无法连接授权服务”，先运行上面的 `-Action status`；没有启动时运行 `-Action start`，再重新激活。生成卡密不等于启动了授权服务。自定义端口时，后台命令的 `-Port` 与程序旁 `license-server.json` 中的端口必须一致。

需要实际加载测试时，先退出旧版启动器及游戏，避免同时加载不同版本，再使用本地验证版的 `phoenix.exe`。DLL 在初始化任何游戏功能前独立向本地服务验权；运行中失效后阻止新增功能调用，并请求现有安全卸载流程。恢复操作需要游戏线程执行；加载画面或游戏无响应时可能延迟卸载，授权功能仍保持禁用。续期后重新验证并重启该版本。

## 管理卡密

```powershell
./licensing/manage-local.ps1 -Action list
./licensing/manage-local.ps1 -Action extend -LicenseId '这里填license_id' -Seconds 3600
./licensing/manage-local.ps1 -Action revoke -LicenseId '这里填license_id'
./licensing/manage-local.ps1 -Action restore -LicenseId '这里填license_id'
./licensing/manage-local.ps1 -Action unbind -LicenseId '这里填license_id'
```

- `extend`：未激活时增加可用时长；已激活时从“原到期时间与当前服务端时间中较晚者”延长。不会自动解除吊销。
- `revoke`／`restore`：吊销／恢复该许可证；恢复不会自动延长期限。
- `unbind`：解除设备绑定并使旧会话失效。再次激活保留原到期时间。
- 当前默认绑定一台设备。重新激活会轮换会话凭据；同一会话可供启动器和 DLL 刷新，刷新时不轮换凭据，避免两者并发失效。

管理操作只通过本机命令执行，没有对外管理 HTTP 接口。

## 验证重点

| 操作 | 预期 |
| --- | --- |
| 没有激活，执行 `--license-check` | `activation_required` |
| 错误码 | 拒绝激活，DLL 不初始化功能 |
| 正确码 | 显示授权到期时间，本机时区 |
| 连续运行至到期 | 授权停止，不能靠不关闭程序延长 |
| 运行中断开本地服务 | 最多使用到上一次签名凭证截止时间，重启程序必须重新联网 |
| 后台续期 | 下次有效刷新取得新期限；已经停止的实例需重新验证／启动 |
| 后台吊销 | 下次刷新拒绝，断网时最迟在旧凭证截止时间停止 |
| 修改缓存文件、替换错误公钥签名、重放旧挑战值 | 验证失败 |

`--check` 仍只检查 DLL 文件存在，用于打包诊断，不等于授权或完整性校验。

自动化测试入口：

```powershell
./tests/run-license-tests.ps1
```

测试使用临时数据库及目录，覆盖服务端事务、到期、续期、吊销、换机、时间回拨检测、Windows 原生验签、字段篡改、加密缓存及断网；不会进入游戏或操作游戏角色。游戏内运行、到期恢复和卸载还需要实际场景验证。

## 以后迁移服务器

授权核心与 HTTP 适配已分开，服务端的 `create_app()` 提供 WSGI 入口，可迁移到服务器。当前本地入口使用 Python 的开发服务器，正式部署时应改用生产 WSGI 服务并置于 HTTPS 之后。[Python WSGI 文档](https://docs.python.org/3/library/wsgiref.html)

客户端的 `license-server.json` 只设置服务根地址，例如当前的 `http://127.0.0.1:17891`；它不能关闭授权或替换公钥。HTTP 只对本地验证构建中的字面地址 `127.0.0.1` 放行；其他 HTTP 地址会被拒绝。支持正常校验证书的 HTTPS，禁止跟随重定向。改地址后须重新激活，因为缓存也绑定服务地址和签名公钥。

正式迁移步骤：部署服务和数据库、配置 HTTPS、改用正式签名密钥、以正式公钥重新编译客户端、移除本地 HTTP 编译分支，再完成限流、运维、密钥轮换、正式发布构建、加壳和代码签名。现在的构建脚本只接受 Development 和 Local，尚不生成正式受保护版。

本地服务仍受本机所有者控制；回拨检测只能发现已记录时间之后的普通回拨，无法阻止拥有本机权限的人修改数据库、服务代码或恢复整机快照。这里验证的是授权功能流程，正式的可信时间和管理权限边界要在服务器迁移后建立。
