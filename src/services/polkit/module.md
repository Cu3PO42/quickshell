name = "Quickshell.Services.Polkit"
description = "Polkit API"
headers = [qml.hpp]
-----

## Purpose of a Polkit Agent

PolKit is a system for privileged applications to query if a user is permitted to execute an action.
You have probably seen it in the form of a "Please enter your password to continue with X" dialog box before.
This dialog box is presented by your *PolKit agent*, it is a process running as your user that accepts authentication requests from the *daemon* and presents them to you to accept or deny.

This service enables writing a PolKit agent in Quickshell.

## Implementing a Polkit Agent

The backend logic of communicating with the daemon is handled by the @@PolkitAgent object.
It exposes incoming requests as properties and provides appropriate signals.

### Flow of an authentication request

Incoming authentication requests are queued in the order that they arrive.
If none is queued, a request starts processing right away.
Otherwise, it will wait until prior requests are done.

A request starts by emitting the @@PolkitAgent.authenticationRequestStarted signal.
At this point, information like the action to be performed and permitted users that can authenticate is available.

An authentication *session* for the request is immediately started, which internally starts a PAM conversation that is likely to prompt for user input.
* Additional prompts may be shared with the user by way of the @@PolkitAgent.subMessageChanged signal and the @@PolkitAgent.subMessage property. A common message might be 'Please input your password'.
* An input request is forwarded via the @@PolkitAgent.inputRequestChanged signal and @@PolkitAgent.inputRequest property. Note that the request specifies whether the text box should show the typed input on screen or replace it with placeholders.

User replies can be submitted via the @@PolkitAgent.submit method.
A conversation can take multiple turns, for example if second factors are involved.

If authentication fails, we automatically create a fresh session so the user can try again.
The @@PolkitAgent.authenticationFailed signal is emitted in this case.

If authentication is successful, you receive the @@PolkitAgent.authenticationSucceeeded signal. At this point, the dialog can be closed.
If additional requests are queued, you will receive the @@PolkitAgent.authenticationRequestStarted signal again.

#### Cancelled requests

Requests may either be canceled by the user or the PolKit daemon.
In this case, we clean up any state and proceed to the next request, if any.

If the request was cancelled by the daemon and not the user, you also receive the @@PolkitAgent.authenticationRequestCancelled signal.

## Limitations

- If multiple authentication requests are queued, we cannot determine which one is canceled (if one is canceled)
