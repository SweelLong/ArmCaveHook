# 项目状态

已实现的能力包括 Apple Mach-O 和 Android ELF 注入、AArch64 解码/重定位/CFG 分析、
4/12/20 字节远跳序列、独立插件代码段与数据段、动态符号和 PLT/GOT 查找、字节签名、
Function IR 与函数发现、Mach-O chained-fixup 维护、Objective-C 与 Swift metadata 分析、
`patch.toml` 以及结构化诊断。

后续计划包括代码签名流程说明、字节签名稳定性指导、可选的跨版本地址辅助工具、轻量 C++ 插件
工具集，以及对 Android ELF 的 DT_RELR 和 `eh_frame` 等支持扩展。
