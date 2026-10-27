# Everything EQ

Everything EQ is a parameter EQ plugin that tries to do the basic things very well,
while piling a lot of options on top. It has:

*   8 band parametric EQ with Low Pass, High Pass, Low Shelf, High Shelf, Peak,
    Band Shelf, Band Pass, and piles on Tilt, Fletcher-Munson, Vowel Formants, and K-weight.
*   Dynamics detector with sidechaining, assignable to gain, frequency or morphing targets.
*   Frequency curve displays, that update in response to dynamics.
*   Very basic in/sidechain-in/out routing (that's what Logic supports) but you can split
    the output across L/R channels for implementing crossovers downstream of the plugin,
    e.g. for processing a mono bass track.

You can use the extra features to do things like:
*   duck the midrange of the guitars in response to the vocal track
*   split the snare into transient and sustain parts
*   gate a high shelf on a snare to cut hi-hat bleed
*   dynamically boost low/high shelf on drums in time with beats
*   do touch-wah style filters with vowel formants on guitars

It's probably too much but I think it's fun.

I've only tested in Logic so far but it targets Windows and Mac, VST and AU etc.

Apache 2.0 licensed.
