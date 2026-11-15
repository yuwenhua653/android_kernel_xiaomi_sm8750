# Redmi K90 / annibale OSS kernel

本项目以 LineageOS 的 Qualcomm SM8750 三仓库源码为基线，逐步移植
MiCode `annibale-w-oss` 的 Redmi K90 设备支持。

当前为初始化阶段。尚未完成 K90 内核、外部模块及完整 DTBO 编译，
没有可刷入版本，也未验证启动或硬件功能。

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

## 适配进度与后续工作

- 已建立三个 Fork、本地工作目录和统一开发分支。
- 已完成目录迁移所需的 Soong namespace 调整及静态检查。
- 尚未导入 MiCode K90 硬件补丁。参考核心提交为
  `MiCode/Xiaomi_Kernel_OpenSource@f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea`，
  设备树为 `MiCode/kernel_devicetree@32fd4ddcedd46baa579643b3894c37c0fccccb91`。
- DT 依赖初步分析：七个新增板级 DTS/DTSI 之外，还需迁移真实的
  `mca_adsp_glink` 与 `usb_nop_phy` 节点、显式 USB PHY 宏头文件依赖，
  并逐项审查 PMIC、热管理和外设差异；不能仅复制 overlay 即宣称支持 K90。
- 需要补齐/核对 HWID、MCA 充电、Goodix 触摸及其他设备专属驱动。
- 需要接入 ROM 内核源码编译、外部模块、DTB/DTBO 和模块分区装配。
- 继承的 Bazel 文件仍有 `//vendor/qcom/sm8750-*` 标签；尚未配置其
  独立工作区映射，当前不提供已验证的 Bazel 构建命令。
- 核心 Makefile 继承的 `video-kernel` 头文件目录与模块仓库中的
  `video-driver` 名称不一致，需要在模块构建接入时继续核查。

每次硬件移植应记录来源提交，并将编译检查、DT 合并检查和真机验证
分别记录。完整构建及真机验证前，不发布可刷入版本。
