# mountains&seas · 山&海 0.4.0 · 基岩版服务端（BDS）

作者：**peakarma**。版本 **0.4.0**，地形内核 **2.225**，结构规则 **1.0.78**。

这是 **LeviLamina 服务端**（BDS）版本，不是 Windows 客户端版。它把同一个地形内核和结构运行时接到专用服务器上：服务器生成的地形与客户端版逐方块一致，但不需要客户端安装任何模组。

| Minecraft 基岩版服务端 | LeviLamina | 安装包 |
|---|---|---|
| 1.26.51.01 | 26.51.9 | `mountains-and-seas-0.4.0-BDS-MC1.26.51.01.zip` |
| 1.26.20.05 | 26.20.7 | `mountains-and-seas-0.4.0-BDS-MC1.26.20.04.zip` |

需要 Windows x64 服务端主机和支持 AVX2 的 CPU。Linux 主机请用 Wine 或 Windows 虚拟机运行 BDS 本体——模组本身是原生 Windows DLL，链接阶段依赖 LiteLDev 的 `prelink.exe`，无法在纯 Linux 上构建或加载。

## 安装

1. 停服，备份 `worlds/`、`mods/`、`plugins/`。
2. 把 ZIP 解压到 BDS 根目录，得到 `mods/mountains&seas/`。
3. 确认 `mods/mountains&seas/manifest.json` 的 `platform` 是 `server`、`entry` 是 `karma-worldgen-bds.dll`。
4. 准备地形数据并创建世界（见下节）。**不要**直接开一个原版世界——地形数据缺失时模组会拒绝启用并写诊断。

## 准备世界（每个种子一次）

```bat
python -X utf8 "mods\mountains&seas\ports\bds-server\install_world.py" ^
  --server-dir "C:\bedrock-server" ^
  --world "world" ^
  --seed 20260415
```

它会烘焙该种子的地形数据到 `plugins/karma-worldgen/`，并在 `worlds/world/` 生成一个匹配的世界（海平面 Y100，建造范围 Y-64..511，含高度行为包）。同一份安装包可服务任意种子；地形数据按种子烘焙，之后不可变。

只检查、不改文件：

```bat
python -X utf8 "mods\mountains&seas\ports\bds-server\install_world.py" ^
  --server-dir "C:\bedrock-server" --world "world" --verify
```

需要 Python 3.13 与 Node.js 24（烘焙种子用）；只需在准备世界时可用，运行时不需要。

## 服务端配置

`server.properties` 建议：

```properties
level-name=world
level-seed=20260415
online-mode=true
allow-cheats=false
```

`level-seed` 必须与 `--seed` 一致，否则模组拒绝启用（这是故意的：地形数据与种子绑定，错配会生成错误地形）。

地形行为包已由 `install_world.py` 写入世界的 `world_behavior_packs.json`；服务端会自动加载。

## 结构与地形的关系

默认只启用地形。结构运行时需要显式开启，因为它在世界生成里安装了原生钩子：

`plugins/karma-worldgen/structures/config.json`

```json
{
  "automaticStructures": true,
  "smallFeatures": true
}
```

`automaticStructures` 控制村庄、神庙、矿井等大型结构；`smallFeatures` 控制化石、植被等小结构。两者默认 `false`，请先在测试世界验证再上生产。

## 控制台命令

服务器控制台（不是游戏内聊天）可用：

| 命令 | 作用 |
|---|---|
| `terrain status` | 地形状态、种子、加载请求与错误计数 |
| `terrain checkblocks` | 校验当前方块状态表与地形配方一致 |
| `terrain probe <x> <z>` | 对指定区块做一次显式几何探测 |
| `terrain release` | 释放探测加载范围 |
| `structurelab status` | 结构运行时状态 |
| `structurelab atlas` | 结构图集与区域计划统计 |

## 已知限制

- 一个 DLL 只注册一次模组入口，所以地形与结构由同一个宿主协调；两者不能分别安装。
- 地形数据与种子、内核版本绑定。升级模组后需要重新运行 `install_world.py`（`--force` 只覆盖插件数据，不覆盖已生成的区块）。
- 已生成的区块不会被新配方重写。换配方请用新世界。
- 结构钩子是原生函数挂钩，属于高风险功能；先在测试世界跑通再上生产。

## 诊断

启用失败时看：

- `plugins/karma-worldgen/fatal-runtime.json` / `.txt` — 致命错误与阶段
- `plugins/karma-worldgen/last-run.json` — 最近一次运行统计
- `plugins/karma-worldgen/structures/` — 结构运行时报告
- `logs/` — LeviLamina 服务端日志

模组拒绝启用并写明原因时，通常是：种子不匹配、`terrain.json` 校验失败、游戏线不匹配（例如把 26.20 的 DLL 装在 26.51 服务端）。

原创代码使用 MIT 许可；第三方许可见 `THIRD_PARTY_NOTICES.md`。
