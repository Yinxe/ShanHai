# 基岩版服务端（BDS）移植与运维

本文记录 mountains&seas 从 Windows 客户端模组移植到 **LeviLamina 服务端（BDS）** 的完整过程、遇到的问题与运维要点。面向两类读者：接手构建/发版流程的维护者，以及部署服务端的操作员。

客户端版与 BDS 版共享同一个地形内核和结构运行时。服务端生成的地形与客户端版逐方块一致，区别只在于宿主：客户端模组挂在游戏客户端进程里，BDS 模组挂在专用服务器进程里。

| 组件 | 客户端版 | BDS 版 |
|---|---|---|
| 宿主 | 游戏客户端 | LeviLamina 服务端 |
| 入口 | `ll_mod_load`（地形 + 结构各注册一次） | `ll_mod_load`（单一宿主协调两者） |
| 数据目录 | 客户端实例 | `plugins/karma-worldgen/` |
| 世界 | 客户端存档 | `worlds/<name>/` |
| 构建平台 | Windows x64 | Windows x64（不可交叉编译） |

## 一、为什么必须用 GitHub Actions 构建

BDS 模组的最终链接步骤调用 LiteLDev 的 `prelink.exe`，它只提供 windows-x64 版本，并需要读取 `bedrock_runtime_data`。这意味着：

- 纯 Linux 主机无法完成链接，本地交叉编译路线（xwin + Wine）在拉取 Windows SDK 头文件阶段就失败。
- 唯一的可靠构建路径是 Windows 环境。仓库使用 `windows-2025-vs2026` runner，工具链为 VS2026 LLVM clang-cl。

因此 BDS 端口的所有验证都在 CI 上完成，本地只做代码修改与离线检查。

## 二、移植时的关键设计

### 2.1 单一宿主模式

一个原生模组 DLL 只能导出**一个** `ll_mod_load`。而地形生成器和结构运行时原本是各自独立注册的模块（客户端版正是如此）。直接编译到服务端会导致重复注册。

解决方案是新增 `KARMA_BDS_HOST` 宏：定义它时，两个模块的 `LL_REGISTER_MOD` 被条件编译排除，改为通过 `src/BdsHost.h` 暴露生命周期函数，由 `ports/bds-server/host.cpp` 统一驱动。

```cpp
// src/BdsHost.h
namespace shanhai_bds {
bool terrainLoad();    bool terrainEnable();
bool terrainDisable(); bool terrainUnload();
bool structuresLoad(); bool structuresEnable();
bool structuresDisable(); bool structuresUnload();
}
```

`host.cpp` 里只有一个 `LL_REGISTER_MOD(Mod, Mod::getInstance())`，`load()` 中按顺序调用 `terrainLoad()` → `structuresLoad()`，`enable()` 中调用对应 enable。

### 2.2 独立端口目录

新增 `ports/bds-server/` 而不复用 `ports/windows-client/`。原因：客户端端口用 `PinnedGameImage26_51.h` 钉死了客户端 exe 的时间戳与大小，还引用了 OreUI 客户端符号，这些在服务端都不存在。

服务端端口只需要 `add_requires("levilamina 26.51.9", {configs = {target_type = "server"}})`。

### 2.3 游戏线闸门

服务端 `host.cpp` 会校验 `ll::getGameVersion()` 的游戏线（如 `1.26.51`），不匹配则拒绝加载并写明原因。这防止把 26.20 的 DLL 装到 26.51 服务端上——地形数据格式按游戏线钉死，错配会写出错误布局的区块。

结构运行时的版本闸门放宽为 `SHANHAI_MC_26_51` 前缀匹配：BDS 与客户端对同一个 ABI 携带不同的 build hash，严格相等会误拒。

## 三、移植中解决的三个真实故障

### 3.1 静态初始化阶段分配内存导致进程静默退出

**症状**：装上模组后服务端直接 `exit 70`，stdout 和 stderr 完全为空，连 PreLoader 的第一行日志都没刷出。移除模组则一切正常。

**定位过程**：先隔离环境——用 LeviLamina 自带的 DLL 顶替我们的 DLL，manifest 被正常解析并输出 `1 mod(s) loaded`，证明崩溃在我们 DLL 内部。再用只含空 `ll_mod_load` 的探针 DLL 测试，探针能加载但被拒绝（见 3.2），说明注册路径本身没问题。

**根因**：`src/Worldgen.cpp` 里有若干命名空间作用域的动态初始化：

```cpp
std::shared_ptr<Session> worldSession = std::make_shared<Session>();
std::atomic<std::shared_ptr<Session>> publishedSession{worldSession};
std::unique_ptr<std::once_flag> registryOnce = std::make_unique<std::once_flag>();
```

这些在 DLL 加载时执行，此时宿主分配器尚未就绪；而模组重载了全局 `operator new` 指向 LeviLamina 的分配器，调用即崩。因为发生在日志系统初始化之前，所以没有任何输出。

**修复**：改为首次使用时惰性构造（函数局部 static），静态初始化阶段不再分配：

```cpp
std::shared_ptr<Session>& sessionRef() {
    static std::shared_ptr<Session> session = std::make_shared<Session>();
    return session;
}
```

`once_flag` 无法重置，所以保持指针形式，但同样延迟到首次使用才 `make_unique`。

### 3.2 统一内存分配器是强制的

探针 DLL（不含 `src/MemoryOperators.cpp`）被 LeviLamina 明确拒绝：

```
mountains&seas will not be loaded because it isn't using the unified memory allocation operator
```

LeviLamina 要求所有原生模组使用统一分配器。`src/MemoryOperators.cpp` 定义 `LL_MEMORY_OPERATORS` 并引入 `ll/api/memory/MemoryOperators.h`，导出 `ll_memory_operator_overrided` 符号供加载器校验。这个文件必须编译进 DLL，且整个模组只能有这一个翻译单元定义它。

注意这与 3.1 是同一个机制的两面：必须用统一分配器，但不能在分配器就绪前使用它。

### 3.3 地形数据缺失时进程主动终止

**症状**：`exit 70`，但这次有 `nlohmann::json` 异常痕迹。

**根因**：`plugins/karma-worldgen/terrain.json` 不存在时，`readData()` 抛异常；`calculatorFailure()` 捕获后执行：

```cpp
TerminateProcess(GetCurrentProcess(), 70);
```

这是**故意设计**：地形计算失败时必须立即停止，绝不能返回空地形并让服务端把虚空区块存进世界。所以 `exit 70` 在有数据缺失时是正确行为，不是 bug。

**运维含义**：装完模组必须先跑 `install_world.py` 准备世界与地形数据，不能直接开一个原版世界。

## 四、构建与发版流程

### 4.1 三个 workflow 的职责

| workflow | 文件 | 触发 | 用途 |
|---|---|---|---|
| Windows client release | `build.yml` | `main` 推送 / 手动 / `v*` tag | 客户端包 |
| BDS server build (manual) | `bds-server.yml` | **仅手动** | 服务端包 |
| Tests | `tests.yml` | 仅手动 | 回归测试 |

**测试与发布分离**是刻意设计。回归套件曾因上游测试用例与代码演进脱节（`BasinSite` 结构体新增字段、调色板契约版本上限落后于内核）而失败，导致发布被阻塞。现在测试独立运行，任何一个用例失败都不会挡住出包。

### 4.2 两个平台的包发布到同一个 release

客户端包和服务端包描述的是同一个模组版本，分散在 `v<version>` 和 `bds-v<version>` 两个页面会让操作员找错地方。现在两者都发到 `v<version>`：

- 谁先跑完谁 `gh release create`
- 后跑完的 `gh release upload --clobber`
- 并发时 `create` 失败会重试一次并改走 `upload`，两个 run 不会互相搞失败

实测两个 workflow 同时运行、同时上传，两个包都正确落在同一 release。

### 4.3 xmake 包缓存

`Configure` 阶段是构建的主要瓶颈：xmake 会**从源码编译整个 LeviLamina**（包的 `on_install` 调用 `package.tools.xmake.install`），耗时约 5 分钟，不是下载慢。

缓存 `.xmake\packages` 与 `.xmake\cache` 后：

| 步骤 | 无缓存 | 有缓存 |
|---|---|---|
| Configure | 307s | **5s** |
| 整轮构建 | ~14 分钟 | **~6 分钟** |

两个实现细节值得记录，否则缓存会静默失效：

1. **不能用 `${{ env.LOCALAPPDATA }}`**。它读的是 workflow 的 `env:` 块，不是 runner 的真实环境，会解析成空路径。必须先用 PowerShell 解析真实路径写入 `GITHUB_OUTPUT`，再由 `actions/cache` 读取。
2. **缓存键要覆盖真正钉住依赖的文件**：`xmake.lua`、`tooth.json`、`build-support/*.lua`、`ports/*/xmake.lua`、`src/kernel/xmake.lua`。写成 `**/xmake/*.lua` 匹配不到任何文件。

三个 workflow 使用同一个缓存键，所以任一跑完，另外两个直接命中，不必各自编译一遍 LeviLamina。

## 五、服务端部署

### 5.1 安装模组

停服，备份 `worlds/` 和 `plugins/`，把 ZIP 解压到 BDS 根目录，得到 `plugins/mountains&seas/`。

**模组目录必须是 `plugins/`**。LeviLamina 的 `PreLoaderConfig.json` 把 `modsPath` 指向 `./plugins/`。放进 `mods/` 不会报错，但模组不会被加载——服务端以原版方式静默启动，地形不生效。这是最容易踩的坑。

### 5.2 准备世界（每个种子一次）

```bash
python -X utf8 "plugins/mountains&seas/ports/bds-server/install_world.py" \
  --server-dir "C:\bedrock-server" \
  --world "world" \
  --seed 20260415
```

它做三件事：烘焙该种子的地形数据到 `plugins/karma-worldgen/`、在 `worlds/<name>/` 生成匹配的世界（海平面 Y100，建造范围 Y-64..511，含高度行为包）、写入结构配置。

同一份安装包可服务任意种子；地形数据按种子烘焙，之后不可变。

只检查不改文件：

```bash
python -X utf8 "...install_world.py" --server-dir "C:\bedrock-server" --world "world" --verify
```

需要 Python 3.13 与 Node.js 24，只在准备世界时需要，运行时不需要。

### 5.3 server.properties

```properties
level-name=world
level-seed=20260415
online-mode=true
allow-cheats=false
```

`level-seed` 必须与 `--seed` 一致。错配时模组拒绝启用——这是故意的，地形数据与种子绑定，错配会生成错误地形。

### 5.4 在 Linux 上用 Wine 运行

模组本身是原生 Windows DLL，服务端本体也必须跑在 Windows 或 Wine 上。容器化部署可用 `ghcr.io/liteldev/levilamina-server:latest-wine`。

几个实操要点：

- **必须映射端口**。`docker run` 时漏掉 `-p 19132:19132/udp` 会导致服务端正常启动、日志显示 `Accepting clients on [::]:19132`，但外部客户端完全连不上。
- **Wine 包需要手动准备**。若 lip 下载 Wine 失败（`release-assets.githubusercontent.com` 直连中断），可手动下载后解包到 lip 期望的嵌套路径：`.local/share/lip/wine/wine-11.21-amd64-wow64/wine-11.21-amd64-wow64/`。直接放进 `.cache/lip/` 不会被识别。
- **清空 `data/` 前先备份**。Wine 与已下载的游戏包都在里面，删掉要重新下载约 128 MB。
- **数据目录常归 root**。宿主机普通用户无写权限，文件操作可用一次性容器：`docker run --rm -v "$PWD/data:/data" alpine:3 sh -c '...'`。
- **`docker run -it` 在此类环境可能不可用**（`cannot attach stdin to a TTY-enabled container`）。用 `docker attach` 发控制台命令，或 `docker compose up -d` 后 `docker compose logs`。

### 5.5 结构运行时在服务端的限制

结构运行时安装的是**世界生成阶段的原生函数钩子**，它要求钩子在**世界创建之前**装配好。而 LeviLamina 总是在世界加载完成之后才调用模组的 `enable()`。

因此服务端上结构模块无法启用，会输出：

```
结构模块未生效（服务端需在开服前装配钩子）：Structure generator already exists
```

**这不影响地形生成**。`host.cpp` 明确捕获这个异常并继续提供服务，因为地形与结构是两个独立模块，纯地形是受支持的配置。要启用结构需要改动 LeviLamina 的生命周期，属于后续工作。

## 六、验证状态

以下项目已在真实环境验证：

| 项目 | 结果 |
|---|---|
| 模组加载 | `1 mod(s) loaded` |
| 模组启用 | `1 mod(s) enabled` |
| 地形绑定 | `worldName`、`terrainSeed`、`boundsMode=infinite` 均正确 |
| 世界生成 | 自定义种子（如 `1423716216`）可正常生成，512 高度 |
| 玩家进服 | 连接、出生正常，OP 权限生效 |
| 客户端 release | `v0.4.0` 已发布，62 MB |
| 服务端 release | 同一 `v0.4.0`，3.9 MB |
| 并发发布 | 两个 workflow 同时上传同一 release，无冲突 |
| 包缓存 | Configure 307s → 5s |

以下项目**未**验证或受限：

- 结构运行时在服务端不可用（见 5.5）。
- `effective-structure-rules.json` 中 `world` 字段为空，因为结构模块的 `initialize()` 早于 `enable()` 读取世界名。与 5.5 同源，纯地形运行时无影响。
- 地形实际区块生成需要玩家进入触发；无人时 `cacheComputed` 保持 0，属正常。

## 七、诊断

启用失败时按顺序查看：

| 文件 | 内容 |
|---|---|
| `plugins/karma-worldgen/fatal-runtime.json` | 致命错误与失败阶段 |
| `plugins/karma-worldgen/fatal-runtime.txt` | 同上，纯文本 |
| `plugins/karma-worldgen/last-run.json` | 最近一次运行统计（缓存命中、生成计数） |
| `plugins/vanilla-structure-lab/` | 结构运行时报告 |
| `logs/latest.log` | LeviLamina 服务端日志 |

常见拒绝启用的原因：种子不匹配、`terrain.json` 校验失败、游戏线不匹配（把 26.20 的 DLL 装到 26.51 服务端）。

排查顺序建议：**先隔离模组再怀疑环境**。把 `plugins/mountains&seas/` 移走重启，若服务端正常则问题在模组；若仍异常才是环境问题。这个顺序能在几分钟内排除掉整类误判。

## 八、待办

- 结构运行时在服务端的启用时机：需要在其 `load()` 阶段而非 `enable()` 阶段装配钩子，且必须在世界创建前。
- `PaletteContract` 的 revision 上限需随内核版本持续同步，否则新内核烘焙的世界会被判定为"需要显式目标能力"。
- 回归套件中的硬编码调色板索引（`begin()+179` 等）依赖具体版本的表长度，内核演进后需要重新核对。
