from pathlib import Path

path = Path("Source/UI/MsegPage.h")
text = path.read_text(encoding="utf-8")
old = '''        auto clock = area.removeFromTop (36);
        tempo.setBounds (clock.removeFromLeft (juce::jmax (330, getWidth() / 2)).reduced (2));
        sync.setBounds (clock.removeFromLeft (210).reduced (2));'''
new = '''        auto clock = area.removeFromTop (36);
        const int tempoWidth = juce::jlimit (180, juce::jmax (180, clock.getWidth() - 150),
                                             (int) std::round (clock.getWidth() * 0.56f));
        tempo.setBounds (clock.removeFromLeft (juce::jmin (tempoWidth, clock.getWidth())).reduced (2));
        sync.setBounds (clock.reduced (2));'''
if text.count(old) != 1:
    raise RuntimeError(f"Expected one MSEG tempo row, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("MSEG tempo row made responsive")
