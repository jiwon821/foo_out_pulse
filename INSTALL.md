# Building

1. Clone this repository.

2. Grab SDK version 2022-08-10 from https://www.foobar2000.org/SDK and extract it to `SDK-2022-08-10`. It is the last SDK version that includes `struct t_samplespec` which this component requires.

3. Grab Windows Template Library (WTL) from https://sourceforge.net/projects/wtl/ (WTL10_10320_Release.zip) and extract it to `WTL10_10320_Release`.

4. Open Solution `src\foo_out_pulse.sln` in Visual Studio and insure that `foobar2000_component_client`, `foobar2000_SDK`, `foobar2000_sdk_helpers`, `libPPUI` and `pfc` projects are

        a. part of the solution, and
        b. references of the project `foo_out_pulse`.

    If not, add them.

5. Add `$(SolutionDir)..\WTL10_10320_Release\Include` to the include path of the projects `foobar2000_sdk_helpers` and `libPPUI` (see https://hydrogenaudio.org/index.php/topic,119757.0.html).

6. Build the solution and fix any errors I forgot to document. The component will be copied to the `foobar2000-$(Platform)\components" directory.

# Testing

TODO
