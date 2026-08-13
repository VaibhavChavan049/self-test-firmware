# Reference examples — put your TI-Rex downloads here

**This folder is for reference only.** Nothing here becomes part of the
final self-test firmware. It exists so you have one obvious place to drop
things you download from TI Resource Explorer, and one obvious place to
import them from into CCS.

## What goes here

Whenever a TI-Rex "Import to IDE" button doesn't work, use the download
fallback instead, and unzip the result into this folder:

1. On the TI-Rex example page, find **Download** (near the Import button).
2. Unzip the download directly into this `reference/` folder, so you end
   up with e.g.:
   ```
   firmware/reference/gpio_ex2_toggle/
   firmware/reference/adc_ex1_soc_software/
   firmware/reference/sci_ex3_echoback/
   ```

## How to import from here into CCS

1. CCS → **File → Import → Code Composer Studio → CCS Projects**
2. **Select search-directory** → browse to this `firmware/reference/`
   folder (not the individual example subfolder — point at `reference/`
   itself so CCS finds everything you've dropped in one go)
3. CCS lists every project it found inside → tick the ones you want →
   **Finish**

Each one becomes its own separate project in your CCS workspace. That's
expected — you're not meant to build/flash these, just open the `.c`
files inside each one to see the correct driverlib calls for this device,
then copy what's needed into the **real** project (see below).

## Where the real firmware lives

The actual project you build and flash is a **separate, fresh CCS
project** you create yourself (Empty Driverlib Project template — see
`../README.md` section 1), with the files from [`../src/`](../src/)
copied into it. That project isn't auto-generated here on purpose — CCS
project files (`.project`/`.cproject`/`.ccsproject`) are tied to your
exact installed CCS version, compiler version, and device support
package, so a hand-written one risks not opening at all. Letting CCS
generate it via its own "New Project" wizard guarantees it matches what's
actually installed on your machine — it's a 2-minute step, not worth the
risk of a broken pre-made one costing you more time today.

Once that project exists, copying `src/` into it is the only "real" step
left — everything in this `reference/` folder is scratch space you can
delete once you're done copying driverlib calls out of it.
