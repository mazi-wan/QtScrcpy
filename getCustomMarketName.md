Assumption:

The input structure of "devices.csv" contains few columns. You only need to care of those below
- Brand,
- Device
- Manufacturer
- Model Name

Commands:

1. Build the code to read the "devices.csv" and cache it if possible.
2. For each device window/device cell, Let's matching the "Device", which get through "adb shell getprop ro.product.device", the get the "Model Name" via the "devices.csv" that we read, to replace "DeviceDisplayName" as we changed before.


