# P6 — Harmor 式采样加法合成 / 频谱减法 设计（集 Harmor + Serum + Phase Plant 之长）

日期：2026-09-14
状态：设计已定（用户授权自行决策："按 Harmor/Serum/Phase Plant 集大成者做，别问了"）
归属：路线图 P6（前置：P1 ✓ P2 ✓ P3 ✓，见 `docs/superpowers/plans/2026-09-07-p2-p6-roadmap.md`）
参考系：Harmor（图像式加法重合成 / 谐波遮罩）、Serum（视觉反馈 / 拖拽导入 / 干净的可见性）、Phase Plant（双发生器 + 混音、模块化、per-voice 思路）

## 背景与问题

引擎本已是"Harmor 架构"：`WavLoader → PartialTracker/STFTAnalyzer → PartialData(frames of Partial, freq/amp/phase) → ResynthesisEngine → resynthBuffer_`。但：

1. **编辑不可闻**：`SpectrumEditorCanvas`（EDITOR/3D 视图）已挂载、有完整笔刷/选择/undo，但 `onPartialEdited` **从未接线**；且 editor timer 每 tick 调 `setPartials()` 会把编辑覆盖。
2. **频谱链是哑的**：prism/blur/harmonizer/multiband/partialMod 全部处理 `synthPartials_`，而该缓存只被 morph/wavetable 填充，**从不从加载的采样填充**（`PluginProcessor.cpp:762-777` 源码自注 placeholder）。
3. **死控件**：`TimbrePanel` 的 bright/blur/hpf 无回调（sub 已接）；`timbreBlendSlider_`(BLEND) 无回调；`modSlots[11]/[12]` 有 `timbre_a_brightness`/`timbre_b_blur` 目标但无人写。
4. **`PartialEditorCanvas`**（time×partial 谐波图）完全未实例化，测试/CMake 双注释。

用户已明确选择路线：**真正实时加法合成**（不是离线重合成、不是掩码叠加）。

## 决策记录（用户逐条确认）

| 决策点 | 结论 |
|---|---|
| 试听架构 | **B) 真正实时加法合成**（partial 直接驱动发声） |
| 第一批音源范围 | **A) 单帧谐波集**（不做时间维度；时变图像留后续批次） |
| 与旧播放关系 | **A) 新增 SYNTH 模式开关**（保留采样播放，可 A/B 对比、可退回） |
| 复音/CPU | **A) 复音 + 限量 partial**（每 voice 取幅度前 N，N 默认 128；voice 上限 K 默认 16） |
| 编辑器 | **A) `SpectrumEditorCanvas`**（freq×amp 笔刷/选择/移动 partial/undo，匹配单帧） |
| 引擎集成 | **A) 新建独立 `AdditiveSynth` 类**（与现有 `VoiceManager` 并存，隔离、可单测） |

## 目标形态

加载采样后，切到 **SYNTH 模式**：声音由该采样的谐波集实时加法合成（每个 MIDI 音移调整套谐波）。在频谱 EDITOR 视图里笔刷/拖动 partial → **立即改变发声**。Harmor 的"减法"语义由此成立：抹掉某谐波 → 对应频点能量消失。

集大成要点：
- **Harmor 内核**：partial 级加法振荡 + 谐波遮罩式编辑（画出来的谱就是声音）。
- **Phase Plant 思路**：A/B 双谐波集 + BLEND（`DualTimbre` 六种混合模式）；控件在 partial 域（bright/HPF/blur）以非破坏方式叠加在编辑结果之上。
- **Serum 思路**：保持现有"Spectrum 常驻 + 视图下拉"的可见性（P3 已铺好）；编辑即时可见即所得。

## 设计

### 1. 新引擎 `AdditiveSynth`（`src/dsp/AdditiveSynth.{h,cpp}`）

```cpp
namespace ana {

class AdditiveVoice : public juce::MPESynthesiserVoice {
    // 每 voice：ratio = noteFreq/rootFreq；对前 N 个 partial 各持一个递归 phasor
    // (cos/sin 旋转，无 std::sin)；求和 × ADSR × velocity。
    // 复用 VoiceManager 的包络语义（attack/decay/sustain/release 秒）。
};

class AdditiveSynth : public juce::MPESynthesiser {
public:
    static constexpr int kMaxPartials = PartialDataSIMD::kMaxPartials; // 512
    AdditiveSynth();
    // 自有方法（非 JUCE override）；内部沿用 VoiceManager::prepare 的写法：
    // 只在 instrument 级设一次采样率，绝不调用会杀音符的逐 voice 速率设置
    void prepare(double sampleRate);

    // 消息线程：清洗（NaN 剔除、freq→[0,Nyquist)、按 amp 降序取前 maxPartials）后发布
    void setPartials(const PartialDataSIMD& partials);
    // 音频线程块首：把快照稳定到本地（seqlock 读，永不阻塞）
    void beginBlock();

    void setMaxPartials(int n);        // 默认 128
    void setMaxVoices(int n);          // 默认 16（≤ 构造时的 voice 数）
    void setRootNote(int note);        // 默认 60
    void setRootFineTune(float cents); // 默认 0

    int  getActivePartialCount() const;
    bool hasPartials() const;

    // 只读快照，供 voice 渲染
    struct Snapshot {
        float frequency[kMaxPartials];
        float amplitude[kMaxPartials];
        float phase[kMaxPartials];
        int   count = 0;
        float rootHz = 261.6256f;
        float fineRatio = 1.0f;
    };
};
}
```

- **无锁发布**：`setPartials` 写"非活动缓冲"→ `seq++`（奇数）→ 写 → `seq++`（偶数）；`beginBlock` 读 seq、拷贝、再读 seq，若变化则重试（写极稀有，最多重试几次）。音频线程块首一次 6KB 拷贝，可忽略。
- **CPU 上限**：仅遍历 `count` 个 partial；`maxPartials` 与 `maxVoices` 可配。每个 partial 每样本一次复数旋转（4 乘 2 加）。
- **不含 `PluginProcessor.h`**（clap 铁律，可进测试目标）。

### 2. Processor 集成（`PluginProcessor`）

新成员：
```cpp
ana::AdditiveSynth additiveSynth_;
std::atomic<bool>  synthMode_{ false };
ana::PartialDataSIMD sourcePartials_;   // 取自 engine 分析结果（消息线程）
ana::PartialDataSIMD editedPartials_;   // UI 编辑后的当前谐波集（消息线程）
```

新 API：
- `bool isSynthMode() const; void setSynthMode(bool);`
- `void setEditedPartials(const PartialDataSIMD&); const PartialDataSIMD& getEditedPartials() const;`
- `void resetPartialsFromEngine();` — 取源帧重填 `editedPartials_` 并推给 `additiveSynth_`
- `int getActivePartialCount() const;`（UI/状态栏显示）

`loadFile`：分析后，源帧选择 = **全帧中总能量（Σamp²）最大的帧**（比 last-frame 更代表音色），填充 `sourcePartials_`/`editedPartials_`，推给 `additiveSynth_`。

`processBlock`：
- 同步 `additiveSynth_.setRootNote/FineTune`（来源 `rootNoteParam_`/`rootFineTuneParam_`）。
- 若 `synthMode_`：`additiveSynth_.renderNextBlock(voiceBuffer, midiMessages, 0, numSamples);` 并**跳过 resynthBuffer 循环播放**（保持下游 effectsChain/vocalProcessor/multiFilter/master/ADSR/metering/scope 原样复用）。
- 否则：现有采样播放路径不变（含 `synthPartials_`/spectral effects 行为不变）。

`prepareToPlay`：`additiveSynth_.prepare(sampleRate)`。

### 3. 编辑器接线（`PluginEditor`）

- `timerCallback` 中，仅当 `!synthMode_`（且非编辑中）才用 engine 帧刷 `spectrumEditorCanvas_.setPartials()`；SYNTH 模式下改为推送 `audioProcessor.getEditedPartials()`（或不再每 tick 推，只在加载/重置时推），**杜绝覆盖用户编辑**。
- `spectrumEditorCanvas_.onPartialEdited = [this](const ana::PartialDataSIMD& p){ audioProcessor.setEditedPartials(p); };`
- SYNTH 开关按钮放在**传输/状态条**（LOAD 旁），toggle 风格，与 PRISM/BLUR/HARM 按钮同款观感；状态栏显示 `SYNTH: n partials`。

### 4. 控件接线（Round 2）

- `TimbrePanel`：bright（逐 partial 幅度倾斜，`amp *= pow(f/f0, tilt)`）、HPF（低于截止置零）、blur（复用 `BlurEffect::setHarmonicBlur`）三个死旋钮接线；A/B 两侧分别作用。
- BLEND（`timbreBlendSlider_`）：实例化 `DualTimbre`，对 A/B 两个编辑谐波集做幅度混合（Fade 默认，模式后续可扩）。
- 生效顺序：A/B 编辑集 → bright → HPF → blur → `DualTimbre` blend → `AdditiveSynth::setPartials`。
- **全部在消息线程、参数/编辑变化时重算**（≤512×3 量级），音频线程零额外开销。

### 5. UI 归属（沿用 P3 分页）

- SYNTH 模式开关 + BLEND + Timbre A/B 控件都在 **TIMBRE 页**（BLEND 已有底部横条位置）。
- 编辑器在频谱区的 **EDITOR / 3D** 视图（P3 已常驻）。
- 帧选择器（时变批次再做）预留频谱区一条细行。

## 风险与对策

- **覆盖编辑**：timer 推送逻辑必须先于接线修好，否则用户一笔就被刷掉（Round 1 必含）。
- **CPU**：128 partial × 16 voice = 2048 振荡器；如 CI/宿主吃紧，调低 `setMaxPartials` 默认值即可，接口已留。
- **相位**：编辑改变了 amp/freq，相位沿用分析值；不追求相位连续（Harmor 亦然）。
- **clap 陷阱**：`AdditiveSynth.*` 严禁 include `PluginProcessor.h`；测试文件同样。
- **首次/极小窗口**（pluginval giant-resize）：不新增几何，风险低。
- **NaN/越界**：`setPartials` 统一清洗（`std::isfinite`、freq∈[0,Nyquist)、amp∈[0,1]）。

## 测试与验收

1. **单元测试**（`tests/test_additive_synth.cpp`，不 include PluginProcessor.h）：
   - 已知单 partial 谐波集渲染 → 输出 RMS 非零且主频正确（FFT/过零）。
   - 空集 → 静音、无崩。
   - amp=0 抹掉某 partial → 该频点能量显著下降（减法语义）。
   - `setMaxPartials` 上限截断（取幅度前 N）。
   - 抢音/复音：note on/off 生命周期不崩、voice 数受 `maxVoices` 约束。
   - 并发：渲染线程读 + 消息线程 `setPartials` 反复写 → 无崩、无撕裂断言（smoke）。
   - 频率移调：note 升 12 半音 → 主频 ×2（±容差）。
2. **回归**：现有 557 用例 0 失败；pluginval strictness **3** 不回归（Round 1），Round 2 搭车升 4。
3. **手工**：加载 WAV → 切 SYNTH → 弹键可闻；EDITOR 里抹/画 partial → 听感实时变化；关 SYNTH → 回到原采样播放。

## 分批（2 轮 CI，符合路线图 2–3 轮估计）

- **Round 1**：`AdditiveSynth` + 单测 + SYNTH 模式 + 源帧填充 + 编辑器接线（编辑→可闻）+ timer 覆盖修复。
- **Round 2**：bright/blur/hpf/BLEND + `DualTimbre` 实例化 + 状态栏 partial 数 + pluginval 4 搭车。

## 显式不做（本批 YAGNI）

- 时间维度（时变图像回放）、逐帧插值。
- `PartialEditorCanvas` 合并/替换（保留待时变批次）。
- 真共振峰/多模式 `DualTimbre` UI（先 Fade）。
- 粒子/DNA/HPSS 接线（属 P5）。
