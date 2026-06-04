# foo_out_pulse

Native event-based PulseAudio output for foobar2000, for use in Wine.

## Usage

This repository does not contain the required PulseAudio libraries. For now, you will need to either get pre-compiled libraries for Windows (from e.g. [this repository](https://github.com/pgaskin/pulseaudio-win32)) or compile PulseAudio for Windows yourself, which I have not successfully done. After you have the required libraries, place them under the `components/pulse` directory in the foobar2000 installation path.

The required libraries are:

    libFLAC-8.dll
    libgcc_s_sjlj-1.dll
    libintl-8.dll
    libogg-0.dll
    libpcre-1.dll
    libpcreposix-0.dll
    libpulse-0.dll
    libpulsecommon-15.0.dll
    libsndfile-1.dll
    libssp-0.dll
    libvorbis-0.dll
    libvorbisenc-2.dll
    libwinpthread-1.dll

If the libraries cannot be found or loaded correctly, the component will print an error message regarding `libpulse-0.dll` in the foobar2000 console.

## Setup

By default, this component connects to `tcp4:127.0.0.1` (port 4713 by default) using PulseAudio's native TCP protocol. The connection string can be specified in advanced settings (Playback -> PulseAudio output -> PulseAudio server). I have not been able to get IPv6 or Unix socket connections to work under Wine.

To configure the PulseAudio server running under PipeWire to listen on the TCP socket, specify the socket in `pipewire-pulse.conf` (or somewhere like `~/.config/pipewire/pipewire-pulse.conf.d/70-pulse-properties.conf`):

    pulse.properties = {
        server.address = [
            "unix:native",
            "tcp:127.0.0.1:4713"
        ]
    }

You might also want to symlink the PulseAudio cookie into your Wine prefix:

    WINEPREFIX=/opt/wine/foobar2000
    mkdir -p "${WINEPREFIX}/drive_c/users/${USER}/.config/pulse"
    ln -fs ~/.config/pulse/cookie "${WINEPREFIX}/drive_c/users/${USER}/.config/pulse/cookie"

If you don't symlink the cookie, it seems to get copied automatically by something. I have no idea why that happens.

For the icon to appear in the mixer, you will need to place a png of foobar's icon named `foobar` in, for example, `~/.icons/hicolor/256x256/apps/foobar.png`

## Building

These steps were performed on Windows 11 in June 2026. Your mileage may vary.

1. Download and install Visual Studio Community. You can download Visual Studio bootstrappers from [here](https://learn.microsoft.com/en-us/visualstudio/install/create-a-network-installation-of-visual-studio?view=visualstudio#download-the-visual-studio-bootstrapper-to-create-the-layout), or directly from [this link](https://aka.ms/vs/stable/vs_community.exe).

2. Under component selection, install the "Desktop development with C++" bundle under "Workloads -> Desktop & Mobile".

3. Grab SDK version 2022-08-10 from the [foobar2000 Software Development Kit page](https://www.foobar2000.org/SDK) and extract it to `SDK-2022-08-10`. This is the last SDK version that includes `struct t_samplespec` which this component requires.

4. Grab Windows Template Library (WTL) from [SourceForge.net](https://sourceforge.net/projects/wtl/) (WTL10_01_Release.zip at the time of writing) and extract it to `WTL10_01_Release`.

5. Open solution `src\foo_out_pulse.sln` in Visual Studio. It might ask you to upgrade the platform toolset from something to something. Do that, or install the appropriate platform toolset. I just retargeted everything to a new toolset.

6. Ensure that `foobar2000_component_client`, `foobar2000_SDK`, `foobar2000_sdk_helpers`, `libPPUI` and `pfc` projects are dependencies of the project `foo_out_pulse` (Solution 'foo_out_pulse' -> Properties -> Project Build Dependencies).

7. Check that `$(SolutionDir)..\WTL10_01_Release\Include` is in the include path of the projects `foobar2000_sdk_helpers` and `libPPUI` (see https://hydrogenaudio.org/index.php/topic,119757.0.html) (Project Properties -> Configuration Properties -> C/C++ -> General -> Additional Include Directories).

8. Build the solution. Only `Release|Win32` is tested for now.

## Known bugs

- With some sound cards, playback seems to be sped up when Pulseaudio is using its default timer based scheduling mode. You could try switching to interrupt scheduling (tsched=0) or try enabling the workaround in Advanced Preferences.

## Advantages

Foobar works very well in Wine, but Wine's default Pulseaudio output has some disadvantages. Compared to using the standard output, this component

- May be less liable to cause audio dropouts when performing IO-intensive tasks such as tag writing, as it buffers as much as possible on the Pulse end,
- Doesn't resample unnecessarily, making the most of Pulseaudio's `avoid-resampling` option,
- Can show foobar's icon in the system mixer,
- Integrates with Pulseaudio's volume control.

## Todo

Currently it

- Only connects to IPv4 TCP sockets and doesn't support connecting via Unix socket like normal programs
- Probably doesn't properly pass through channel mappings from source files correctly
