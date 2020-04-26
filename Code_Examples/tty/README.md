# Why are these here?

I added the tty drivers from the sample repos with some slight modifications to make them run better. I made the device major number dynamic and added a close method to the serial version.

### Current Bugs

If you insert the tty driver, then remove it, then try to reinsert it there will be an error because it still doesn't clean itself up properly. You should be able to use it on the first try though. See my book notes on tty drivers for some more details. 
