## Hardware capabilities:
XIAO ESP32-C3
6 My ID LEDs charlieplexed
6 Your ID LEDs charlieplexed
1 Send LED (to check what you're sending)
1 Recieve LED (to check what you're recieving)

For more information on how the components are connected, check ../pegoku-fallout-badge's schematic.

## Functionality

The device has many modes/states it can be in.

### My ID input mode

After powering on, it will be in this mode. It will ask for an ID, and the "My Id" LEDs will blink until the user press up or down, and then it will, in binary, display the ID. It goes from 000001 to 111111 (1 to 63)
then, you'll need to press the action button.

If the id is 000000, it will check for which id is available, by "pinging" each other device by using random IDs until the id is "unique".

Now, the My ID LEDs will be set to your id.

### Main Mode

This mode is the default idle mode.

To enter My Id input mode you have to press and hold for 3 seconds both up and down buttons.

If you just press up or down, like one tap, it will enter Who to call mode.

While in this mode it will frequently check for calls to this device, if it recieves one, will go to Call Recieved mode.


### Call recieved mode

In this mode, both RLED and SLED will blink and the user can accept (by pressing the action button) or reject the call by holding the button down for longer. 

If it rejects it, it will go back to main mode.
If it accepts it it will enter Calling Mode.


### Who to call mode

In this mode, the user has to set an ID of which they want to call to, that's the Send LEDs. It will be inputed in the same way as the My ID input mode.
When the ID is set and the user presses the Action button, it will start start the call, and enter the calling mode

If the user Holds down the action button, it will clear the id it was selected and go back to the main mode.

### Calling mode 

In this mode, it will send a message to the device with the ID specified, and wait for the other device to accept or reject it.

If it is rejected, it will go back to main mode.

When the user accepts the call, the call will begin, and bidirectionally the message interaction will be possible.

When the action button is pressed, it will send the "raw" duration of the press, so if the user presses for 312ms, 312ms will be sent.

But if the user presses the IDUp button, it will send a long message, and if they press the IDDown it will send a short message (morse code style).

The communication should be real time, not wait for the message to continue.

After 15 seconds of inactivity (configurable via #DECLARE) it will stop the call.

If the user wants to end the call early, they can press and hold both up and down buttons and it will end the call.

## Software

The project should use a RTOS (e.g. ESP-IDF)

