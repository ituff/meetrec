# 设计交接目录

本目录是给 UI/UX 设计师的交接材料。建议按以下顺序阅读：

1. **[ui-redesign-brief.md](./ui-redesign-brief.md)** — 主设计任务书：产品背景、用户、现状问题、设计目标、信息架构建议、状态契约、交付物、技术约束、文案原则、可用性测试、待决策项。
2. **[appendix-current-ui-inventory.md](./appendix-current-ui-inventory.md)** — 现状盘点：控件清单、状态机、错误码、设置项、托盘行为、术语表、工程确认问题。
3. **[current-ui-annotated.png](./current-ui-annotated.png)** — 带编号区域的当前界面截图（编号对应主文档 3.2 节）。
4. **[current-ui-full.png](./current-ui-full.png)** — 当前界面完整截图（125% 缩放，窗口拉大后截取）。

## 快速摘要

- 产品当前是**窗口录制器**：选一个会议/演示窗口，录成 MP4，可混入系统声音 + 麦克风。
- 窗口录制**不需要虚拟显示器**；界面里遗留的 “Connect display / Virtual display” 是旧概念，建议从主界面移除。
- 当前最大体验问题：主任务不突出、录制状态弱、音频设备易错（且默认值有“只勾麦克风不建音轨”的陷阱）、错误不可恢复、中文用户面对英文技术文案。
- 设计目标：3 步内开始录制；录制中一眼确认状态；停止后 1 步找到文件；错误可行动；原生 Windows 11、键盘可达、Per-Monitor V2 DPI。
- 交付物：IA/流程、全状态高保真、组件与 token、中文文案 deck、托盘/图标资产、可点击原型、开发标注。

## 相关链接

- [产品规格](../../specs/001-mvp/spec.md)
- [需求清单](../../specs/001-mvp/requirements.md)
- [已知限制](../known-limitations.md)
- [发布说明](../../artifacts/dist/VirtualScreenRecorder-1.0.0.0-win64/RELEASE-NOTES.txt)
