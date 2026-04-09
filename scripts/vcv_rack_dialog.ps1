# Dismiss VCV Rack startup dialogs (e.g. cache / "pulire cache e ripartire?").
# Keep this minimal: stray SendKeys before Rack is focused can destabilize automation.
function Invoke-VcvDismissCacheDialog {
    param([int]$Attempts = 2)

    Add-Type -AssemblyName System.Windows.Forms

    for ($a = 0; $a -lt $Attempts; $a++) {
        Start-Sleep -Milliseconds 400
        # Left button = Sì: try accelerator first, then Enter (default focus).
        [System.Windows.Forms.SendKeys]::SendWait("%{S}")
        Start-Sleep -Milliseconds 120
        [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
    }
}
