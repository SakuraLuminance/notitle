#include "EvolutionPanel.h"
#include "CyberpunkTheme.h"
#include <cmath>

namespace ana {

//==============================================================================
EvolutionPanel::EvolutionPanel(AnaPlugAudioProcessor& p)
    : processor(p)
{
    //==============================================================================
    // Population size combo
    popSizeCombo_.addItem("8", 8);
    popSizeCombo_.addItem("16", 16);
    popSizeCombo_.addItem("32", 32);
    popSizeCombo_.addItem("64", 64);
    popSizeCombo_.addItem("128", 128);
    popSizeCombo_.setSelectedId(16, juce::dontSendNotification);
    popSizeCombo_.onChange = [this] { onPopSizeChanged(); };
    addAndMakeVisible(popSizeCombo_);

    //==============================================================================
    // Buttons
    evolve1Btn_.onClick     = [this] { onEvolve1(); };
    evolve10Btn_.onClick    = [this] { onEvolve10(); };
    randomizeBtn_.onClick   = [this] { onRandomize(); };
    loadSampleBtn_.onClick  = [this] { onLoadSample(); };
    saveDNABtn_.onClick     = [this] { onSaveDNA(); };
    loadDNABtn_.onClick     = [this] { onLoadDNA(); };

    addAndMakeVisible(evolve1Btn_);
    addAndMakeVisible(evolve10Btn_);
    addAndMakeVisible(randomizeBtn_);
    addAndMakeVisible(loadSampleBtn_);
    addAndMakeVisible(saveDNABtn_);
    addAndMakeVisible(loadDNABtn_);

    //==============================================================================
    // Labels
    generationLabel_.setText("Gen: 0", juce::dontSendNotification);
    generationLabel_.setFont(CyberpunkTheme::getCyberFont(13.0f, true));
    generationLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::cyan_);
    generationLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(generationLabel_);

    statusLabel_.setText("Click [Randomize] to start", juce::dontSendNotification);
    statusLabel_.setFont(CyberpunkTheme::getCyberFont(11.0f, false));
    statusLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.6f));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    //==============================================================================
    // Initialise fitness cache
    std::fill(cellFitness_, cellFitness_ + 128, 0.0f);

    //==============================================================================
    // Grid layout from initial population size (16 → 4×4)
    updateGridDimensions(16);

    startTimerHz(4);
}

//==============================================================================
void EvolutionPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int rowH = 26;

    // -- Top row: combo + generation label + status --
    auto topRow = area.removeFromTop(rowH);
    popSizeCombo_.setBounds(topRow.removeFromLeft(90).reduced(2));
    generationLabel_.setBounds(topRow.removeFromLeft(120).reduced(2));
    statusLabel_.setBounds(topRow.reduced(2));

    area.removeFromTop(6);

    // -- Button row --
    auto btnRow = area.removeFromTop(rowH);
    const int btnGap = 4;
    const int btnW   = (btnRow.getWidth() - btnGap * 5) / 6;

    auto nextBtn = [&](juce::TextButton& btn) {
        btn.setBounds(btnRow.removeFromLeft(btnW).reduced(1));
        btnRow.removeFromLeft(btnGap);
    };
    nextBtn(evolve1Btn_);
    nextBtn(evolve10Btn_);
    nextBtn(randomizeBtn_);
    nextBtn(loadSampleBtn_);
    nextBtn(saveDNABtn_);
    nextBtn(loadDNABtn_);

    area.removeFromTop(6);

    // -- Population grid --
    gridArea_ = area.reduced(2);
}

//==============================================================================
void EvolutionPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Background
    g.fillAll(CyberpunkTheme::bg_);

    auto& evolver = processor.getDNAEvolver();
    int popSize   = evolver.getPopulationSize();

    // If no population, draw instructions
    if (popSize == 0)
    {
        g.setFont(CyberpunkTheme::getCyberFont(14.0f, false));
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.4f));
        g.drawText("No population — click [Randomize] to begin",
                    bounds, juce::Justification::centred);
        return;
    }

    auto grid = gridArea_;
    if (grid.isEmpty())
        return;

    const int    cells   = juce::jmin(popSize, 128);
    const float  cellW   = static_cast<float>(grid.getWidth())  / static_cast<float>(gridCols_);
    const float  cellH   = static_cast<float>(grid.getHeight()) / static_cast<float>(gridRows_);
    const float  gap     = 2.0f;
    const float  selThk  = 2.5f;

    for (int i = 0; i < cells; ++i)
    {
        int row = i / gridCols_;
        int col = i % gridCols_;

        auto cell = juce::Rectangle<float>(
            static_cast<float>(grid.getX()) + static_cast<float>(col) * cellW + gap,
            static_cast<float>(grid.getY()) + static_cast<float>(row) * cellH + gap,
            cellW - gap * 2.0f,
            cellH - gap * 2.0f);

        // --- Fitness colour: dark→cyan based on fitness ---
        float fit = juce::jlimit(0.0f, 1.0f, cellFitness_[i]);
        auto cellColour = CyberpunkTheme::cyan_
            .withMultipliedBrightness(0.25f + 0.75f * fit)
            .withMultipliedSaturation(0.5f + 0.5f * fit);

        g.setColour(cellColour);
        g.fillRect(cell);

        // Divider lines between cells (neon grid)
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.15f));
        g.drawRect(cell, 1.0f);

        // --- Selection highlights ---
        if (i == selectedIndex_)
        {
            // Primary parent — cyan glow
            g.setColour(CyberpunkTheme::cyan_);
            g.drawRect(cell.toNearestInt(), static_cast<int>(selThk));

            g.setColour(CyberpunkTheme::cyan_.withAlpha(0.12f));
            g.fillRect(cell);
        }
        else if (i == selectedIndexB_)
        {
            // Secondary parent — magenta glow
            g.setColour(CyberpunkTheme::magenta_);
            g.drawRect(cell.toNearestInt(), static_cast<int>(selThk));

            g.setColour(CyberpunkTheme::magenta_.withAlpha(0.12f));
            g.fillRect(cell);
        }

        // --- Index label in bottom-right corner of cell ---
        if (cellW > 30.0f && cellH > 20.0f)
        {
            g.setFont(CyberpunkTheme::getCyberFont(9.0f, false));
            g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
            g.drawText(juce::String(i),
                       cell.removeFromBottom(16.0f).removeFromRight(cellW * 0.5f),
                       juce::Justification::centredBottom, false);
        }
    }

    // --- Selection info overlay ---
    if (selectedIndex_ >= 0 && selectedIndex_ < cells)
    {
        juce::String info = "A: #" + juce::String(selectedIndex_)
            + "  fit=" + juce::String(cellFitness_[selectedIndex_], 3);
        if (selectedIndexB_ >= 0 && selectedIndexB_ < cells)
            info += "  |  B: #" + juce::String(selectedIndexB_)
                  + "  fit=" + juce::String(cellFitness_[selectedIndexB_], 3);

        g.setFont(CyberpunkTheme::getCyberFont(10.0f, false));
        g.setColour(CyberpunkTheme::yellow_);
        g.drawText(info, bounds.removeFromBottom(16).reduced(12, 0),
                   juce::Justification::centredRight);
    }
}

//==============================================================================
void EvolutionPanel::timerCallback()
{
    updateDisplay();
}

//==============================================================================
void EvolutionPanel::onEvolve1()
{
    auto& evolver = processor.getDNAEvolver();
    if (evolver.getPopulationSize() == 0)
    {
        onRandomize();
        return;
    }
    evolver.evolveGeneration();
    updateDisplay();
    statusLabel_.setText("Evolved 1 generation", juce::dontSendNotification);
}

void EvolutionPanel::onEvolve10()
{
    auto& evolver = processor.getDNAEvolver();
    if (evolver.getPopulationSize() == 0)
    {
        onRandomize();
        return;
    }
    evolver.evolveN(10);
    updateDisplay();
    statusLabel_.setText("Evolved 10 generations", juce::dontSendNotification);
}

void EvolutionPanel::onRandomize()
{
    auto& evolver = processor.getDNAEvolver();
    int size = popSizeCombo_.getSelectedId();
    if (size <= 0)
        size = 16;
    evolver.init(size);
    evolver.seedRandom();
    updateGridDimensions(size);
    updateDisplay();

    // Refresh fitness cache
    for (int i = 0; i < evolver.getPopulationSize() && i < 128; ++i)
        cellFitness_[i] = evolver.getDNA(i).fitness;

    statusLabel_.setText("Population randomized — ready to evolve",
                         juce::dontSendNotification);
}

void EvolutionPanel::onLoadSample()
{
    auto fileChooser = std::make_unique<juce::FileChooser>(
        "Select a sample audio file...",
        juce::File{},
        "*.wav;*.aiff;*.mp3;*.flac;*.ogg");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            if (processor.loadSampleAsParent(file))
            {
                statusLabel_.setText("Sample loaded: " + file.getFileName(),
                                     juce::dontSendNotification);
                updateDisplay();
            }
            else
            {
                statusLabel_.setText("Failed to load sample",
                                     juce::dontSendNotification);
            }
        });

    // Keep chooser alive by transferring to a heap owner
    fileChooser.release();
}

void EvolutionPanel::onSaveDNA()
{
    auto& evolver = processor.getDNAEvolver();
    if (evolver.getPopulationSize() == 0)
    {
        statusLabel_.setText("No population to save", juce::dontSendNotification);
        return;
    }

    auto fileChooser = std::make_unique<juce::FileChooser>(
        "Save DNA population...",
        juce::File{},
        "*.dnapop");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            processor.getDNAEvolver().saveToFile(file);
            statusLabel_.setText("DNA saved to: " + file.getFileName(),
                                 juce::dontSendNotification);
        });

    fileChooser.release();
}

void EvolutionPanel::onLoadDNA()
{
    auto fileChooser = std::make_unique<juce::FileChooser>(
        "Load DNA population...",
        juce::File{},
        "*.dnapop");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            processor.getDNAEvolver().loadFromFile(file);
            int size = processor.getDNAEvolver().getPopulationSize();
            updateGridDimensions(size);
            updateDisplay();
            statusLabel_.setText("DNA loaded from: " + file.getFileName(),
                                 juce::dontSendNotification);
        });

    fileChooser.release();
}

void EvolutionPanel::onPopSizeChanged()
{
    int newSize = popSizeCombo_.getSelectedId();
    if (newSize <= 0)
        return;

    auto& evolver = processor.getDNAEvolver();
    evolver.setPopulationSize(newSize);
    updateGridDimensions(newSize);
    updateDisplay();
}

void EvolutionPanel::onCellClicked(int index)
{
    auto& evolver = processor.getDNAEvolver();
    int popSize   = evolver.getPopulationSize();

    if (index < 0 || index >= popSize || index >= 128)
        return;

    // Toggle primary selection; if already selected, clear it.
    if (selectedIndex_ == index)
    {
        selectedIndex_  = -1;
        selectedIndexB_ = -1;
        statusLabel_.setText("Selection cleared", juce::dontSendNotification);
        repaint();
        return;
    }

    // If primary is empty, set it; otherwise shift to secondary
    if (selectedIndex_ < 0)
    {
        selectedIndex_ = index;
    }
    else
    {
        selectedIndexB_ = index;
    }

    // Update status with fitness info
    auto& dna = evolver.getDNA(index);
    juce::String msg = "Selected #" + juce::String(index)
        + "  fitness=" + juce::String(dna.fitness, 3)
        + "  active=" + juce::String(SpectralDNA::computeActiveCount(dna)) + " partials";
    statusLabel_.setText(msg, juce::dontSendNotification);
    repaint();
}

//==============================================================================
void EvolutionPanel::mouseDown(const juce::MouseEvent& event)
{
    // Translate mouse position into grid cell index
    auto pos = event.getPosition();
    auto grid = gridArea_;
    if (!grid.contains(pos))
        return;

    int col = static_cast<int>((static_cast<float>(pos.x - grid.getX())
                                / static_cast<float>(grid.getWidth())) * gridCols_);
    int row = static_cast<int>((static_cast<float>(pos.y - grid.getY())
                                / static_cast<float>(grid.getHeight())) * gridRows_);

    col = juce::jlimit(0, gridCols_ - 1, col);
    row = juce::jlimit(0, gridRows_ - 1, row);

    int index = row * gridCols_ + col;
    onCellClicked(index);
}

void EvolutionPanel::updateGridDimensions(int popSize)
{
    if (popSize <= 8)       { gridRows_ = 2; gridCols_ = 4; }
    else if (popSize <= 16) { gridRows_ = 4; gridCols_ = 4; }
    else if (popSize <= 32) { gridRows_ = 4; gridCols_ = 8; }
    else if (popSize <= 64) { gridRows_ = 8; gridCols_ = 8; }
    else                    { gridRows_ = 8; gridCols_ = 16; }

    selectedIndex_  = -1;
    selectedIndexB_ = -1;
    repaint();
}

void EvolutionPanel::updateDisplay()
{
    auto& evolver = processor.getDNAEvolver();

    // Generation counter
    generationLabel_.setText("Gen: " + juce::String(evolver.getGeneration()),
                             juce::dontSendNotification);

    // Fitness cache
    int popSize = evolver.getPopulationSize();
    for (int i = 0; i < popSize && i < 128; ++i)
        cellFitness_[i] = evolver.getDNA(i).fitness;

    // Fittest info
    if (popSize > 0)
    {
        const auto& fittest = evolver.getFittest();
        statusLabel_.setText(
            "Best fitness: " + juce::String(fittest.fitness, 4)
            + "  |  Gen: " + juce::String(fittest.generation)
            + "  |  Parents: #" + juce::String(fittest.parentA_id)
            + " × #" + juce::String(fittest.parentB_id),
            juce::dontSendNotification);
    }

    repaint();
}

} // namespace ana
