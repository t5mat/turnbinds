<h1 align="center">turnbinds</h1>

<p align="center">
<img src="https://user-images.githubusercontent.com/16616463/149217736-8c7a002e-1b9e-446a-9603-34a3ebe0e0f0.gif">
</p>

Provides turnbinds (`+left/+right`) with customizable yawspeed for CS:GO surf.

The convars `cl_yawspeed`, `cl_anglespeedkey`, which allow control of the turning speed in previous games in the series (1.6, Source), have been made inaccessible in CS:GO. This program aims to solve this by providing binds that turn your view angles by simulating mouse movement.

## Usage

[Download](https://github.com/t5mat/turnbinds/releases/latest/download/turnbinds.exe) and run `turnbinds.exe`.

Settings are stored in `<exe-name>.ini`.

### Basic controls

- **Esc/CTRL+C** - quit
- **Up/down arrows** - navigate
- **Return** - rebind key/edit variable
- **Left/right arrows** - cycle between values/switch on/off

### Variables

- *cl_yawspeed* - The desired turning speed (CS:GO's unchangeable default is 210)
- *sensitivity* - Should match your in-game `sensitivity` value
- *cl_anglespeedkey* - The factor by which *cl_yawspeed* is scaled while holding down the *+speed* key (CS:GO's unchangeable default is 0.67)
- *m_yaw* - Should match your in-game `m_yaw` value

Each of these can have multiple values (space-delimited), which are cycleable while in-game by pressing the *cycle* key (less useful for *m_yaw*).

You can use this program in a number of different ways,
- cycle between multiple *cl_yawspeed* values while using *cl_anglespeedkey* as a constant "slowdown" factor (the more common approach)
- have a single sensible *cl_yawspeed* and cycle between multiple *cl_anglespeedkey* values
- cycle both *cl_yawspeed* and *cl_anglespeedkey*

<blockquote>
    
<p>Optionally, you'd might want to cycle between <i>sensitivity</i> values (increasing both <i>cl_yawspeed</i> and <i>sensitivity</i> for sharper turns).</p>
<p>In this case, the program's <i>sensitivity</i> and the in-game <code>sensitivity</code> need to be kept in sync. This is because the program is external to the game and cannot control your in-game <code>sensitivity</code> - <i>sensitivity</i> values in the program are only used to calculate the correct turning speed.</p>
<p>You'd want to bind your <i>cycle</i> key in-game to cycle between your <i>sensitivity</i> values, and make sure the in-game <code>sensitivity</code> and the program's current <i>sensitivity</i> value are the same.</p>

<p>
So, for this config:
<pre>
cycle             : X1 Mouse Button
cl_yawspeed       : 120 <i>[210]</i> 300
sensitivity       : 1.0 <i>[2.0]</i> 3.0
</pre>
</p>

<p>
Run the following in-game commands:
<pre>
bind MOUSE4 "toggle sensitivity 1.0 2.0 3.0"
sensitivity 2.0
</pre>
</p>

</blockquote>

### Developer settings

Navigate down until selector is hidden and press **Return** to enter developer mode.

- *raw input* (default off) - key detection method (should probably be kept off, see [issue #2](https://github.com/t5mat/turnbinds/issues/2))
- *rate* (default 1000) - maximum number of simulated mouse inputs per second, lower values decrease CPU usage in favor of turn smoothness
- *sleep* (default 3500) - main loop sleep duration (measured in 100ns units, 3500 = 0.35ms), higher values decrease input polling rate and overall CPU usage

## Building

Run `./build` on a Linux machine with Docker installed.
