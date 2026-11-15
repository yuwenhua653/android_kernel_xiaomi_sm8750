# Redmi K90 / annibale OSS kernel

本项目以 LineageOS 的 Qualcomm SM8750 三仓库源码为基线，逐步移植
MiCode `annibale-w-oss` 的 Redmi K90 设备支持。

当前为第一批驱动和设备树移植阶段。尚未完成整核、全部模块链接及
最终镜像装配，没有可刷入版本，也未验证启动或硬件功能。

## 仓库与目录

三个仓库使用 `annibale` 开发分支；`lineage-22.2` 保留初始上游基线。
该上游分支名不代表最终 ROM 版本要求。

| 仓库 | Android 源码中的目录 |
| --- | --- |
| [android_kernel_xiaomi_sm8750](https://github.com/yuwenhua653/android_kernel_xiaomi_sm8750/tree/annibale) | `kernel/xiaomi/sm8750` |
| [android_kernel_xiaomi_sm8750-modules](https://github.com/yuwenhua653/android_kernel_xiaomi_sm8750-modules/tree/annibale) | `kernel/xiaomi/sm8750-modules` |
| [android_kernel_xiaomi_sm8750-devicetrees](https://github.com/yuwenhua653/android_kernel_xiaomi_sm8750-devicetrees/tree/annibale) | `kernel/xiaomi/sm8750-devicetrees` |

在 Android 源码根目录、上述目录尚不存在时，可同步这三个工作仓库：

```sh
git clone -b annibale https://github.com/yuwenhua653/android_kernel_xiaomi_sm8750.git kernel/xiaomi/sm8750
git clone -b annibale https://github.com/yuwenhua653/android_kernel_xiaomi_sm8750-modules.git kernel/xiaomi/sm8750-modules
git clone -b annibale https://github.com/yuwenhua653/android_kernel_xiaomi_sm8750-devicetrees.git kernel/xiaomi/sm8750-devicetrees
```

使用 `repo` 的源码树可参考 `annibale.xml`。它只声明这三个
仓库，不包含 ROM、工具链、设备树或完整的独立内核构建依赖；已有相同
路径的项目需先在自己的 manifest 中处理冲突。

## 初始上游版本

2026-11-15 创建 Fork，保留原作者和提交历史：

| LineageOS 仓库 | 基线提交 |
| --- | --- |
| `android_kernel_qcom_sm8750` | `aefcb6e642bd691d8da6f577cadc794594e63e5c` |
| `android_kernel_qcom_sm8750-modules` | `14ab4c38ac7f07d2b807ca7851500da86d2eb56e` |
| `android_kernel_qcom_sm8750-devicetrees` | `450710a3aac3af54ef46504df2d46afecd6aa0eb` |

核心基线为 Linux 6.6.144 / android15-6.6，KMI generation 8。
保留 Qualcomm 驱动名称、compatible 和内部 `qcom/opensource` 目录。
外部模块的 Soong import 已迁至 `kernel/xiaomi/sm8750`；核心的模块
头文件路径及设备树软链接使用兄弟相对路径，不需要批量替换。

## 第一批移植

| 仓库 | 已加入内容 |
| --- | --- |
| 核心 | 小米 Mi T1 GPIO、独立 Xiaomi SPI IR、可选 AW21024、受配置开关控制的 PMIC bark IRQ 支持、Annibale 配置片段 |
| 模块 | 官方同族 Goodix 9916K / xiaomi_touch 移植候选（默认关闭）；P11 相机 CSIPHY 寄存器表和匹配项 |
| 设备树 | Annibale SoC、充电描述、触摸、红外、NFC、PMIC、热管理、相机和 P11 显示专属 DTS；保持 LOS 公共 DTS 不变 |

核心来源为 `MiCode/Xiaomi_Kernel_OpenSource` 的 `annibale-w-oss`：
`f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea`。触摸来源为官方
`warsaw-w-oss` 同族实现，**不是未公开的 Annibale p11 目录**，仍待真机验证。
详细来源、改动和验证命令分别见各仓库中的移植文档：

- 核心：`Documentation/xiaomi/`、`scripts/xiaomi/compile-annibale-board.sh`。
- 模块：`Documentation/xiaomi/annibale-import.md`、
  `qcom/opensource/camera-kernel/Documentation/annibale-csiphy.md`。
- 设备树：`docs/annibale-import.md` 和来源清单。

`arch/arm64/configs/vendor/annibale_board.config` 仅启用已接入的板级驱动，
不冒充完整量产配置。AW21024 尚无确认的 Annibale DT 节点，保持默认关闭。
触摸必须明确选择其独立 Kconfig/Kbuild，并使用 Annibale 的固件及配置；
不附带其他机型的固件、THP 参数或归一化校准文件。

## 尚待完成

- 补齐/核对 HWID、MCA 充电及指纹等缺失源码和依赖；DT 中有对应节点不代表驱动已可用。
- 验证原厂用户空间接口、触摸固件与硬件版本，接入全部显示/相机所需小米扩展。
- 接入 ROM 内核源码编译、外部模块、DTB/DTBO 与模块分区装配。
- 完成真实 `Module.symvers` 下的模块链接、符号/ABI 核查和整核构建。
- 继承的 Bazel 文件仍有 `//vendor/qcom/sm8750-*` 标签；尚未配置其独立工作区映射。
- 核心 Makefile 的 `video-kernel` 头文件目录与模块仓库 `video-driver` 名称不一致，后续构建接入时核查。

定向目标文件编译、DT overlay 合并及真机功能是不同的验证阶段。
当前检查不能证明启动、充电、相机或触摸已正常，完整构建及真机验证前不发布刷机包。
