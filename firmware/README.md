# Clock Firmware

Blah blah WIP

## Light Layout

The clock layout is a simple 4-digit, 7-segment style display. The unusual thing is, this clock is made from traffic lights, one traffic light set per segment, and each light is individually controllable.

### Light Numbering

The format used to denote light numbers is: digit/segment/light.

Digits are numbered from left to right, with the 10ths digit of the hours being digit "0". For example:

```
 000   111     222   333
0   0 1   1   2   2 3   3
0   0 1   1 * 2   2 3   3
0   0 1   1   2   2 3   3
 000   111     222   333
0   0 1   1   2   2 3   3
0   0 1   1 * 2   2 3   3
0   0 1   1   2   2 3   3
 000   111     222   333
```

Segments are numbered in the standard order of 7-segment displays, starting from the topmost segment as segment "A" and proceeding in a clockwise direction around the entire perimeter segments, numbering them "B", "C", etc... The final, center segment is numbered "G". For example:

```
 AAA 
F   B
F   B
F   B
 GGG 
E   C
E   C
E   C
 DDD 
```

Lights within each segment are are numbered "r", "y" and "g". Lights are laid with reds facing either left, or up.

The colon between hours and minutes is made up of a single light for each dot, denoted X0 and X1.

The full layout is as follows:

```
    0Ar 0Ay 0Ag          1Ar 1Ay 1Ag              2Ar 2Ay 2Ag          3Ar 3Ay 3Ag    
0Fr             0Br  1Fr             1Br      2Fr             2Br  3Fr             3Br
0Fy             0By  1Fy             1By  X0  2Fy             2By  3Fy             3By
0Fg             0Bg  1Fg             1Bg      2Fg             2Bg  3Fg             3Bg
    0Gr 0Gy 0Gg          1Gr 1Gy 1Gg              2Gr 2Gy 2Gg          3Gr 3Gy 3Gg    
0Er             0Cr  1Er             1Cr      2Er             2Cr  3Er             3Cr
0Ey             0Cy  1Ey             1Cy  X1  2Ey             2Cy  3Ey             3Cy
0Eg             0Cg  1Eg             1Cg      2Eg             2Cg  3Eg             3Cg
    0Dr 0Dy 0Dg          1Dr 1Dy 1Dg              2Dr 2Dy 2Dg          3Dr 3Dy 3Dg    
```

### Light Serialization

When light information is sent or stored in the device, it is sent as a series of bytes, where each bit corresponds to a single light in the display.

Lights are serialized in the order digit, segment, light. Lights are stored LSB-first, with the first light being stored in bit 0, the second light in bit 1, etc...

Each light segment is serialized as 3 bits, in the order red, then yellow, then green.

Each 7-segment area is serialzed in segment order, meaning segment A, then B, then C, all the way to segment G.

The middle colon is serialized as two bits, in the order X0, then X1.

An entire clock display state is serialized from the segments, left to right, with the colon in the middle of the between segments 1 and 2.

For example, a bitstream for updating the clock display would be ordered as follows:

```
/----------------------------------------------------------------------------------------------------
|                    Byte 0                     |                    Byte 1                     |
|  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----| etc etc...
| 0Ar | 0Ay | 0Ag | 0Br | 0By | 0Bg | 0Cr | 0Cy | 0Cg | 0Dr | 0Dy | 0Dg | 0Er | 0Ey | 0Eg | 0Fr |
\----------------------------------------------------------------------------------------------------
```

The full bit ordering is as follows (read left-to-right, top-to-bottom):

```
0Ar 0Ay 0Ag 0Br 0By 0Bg 0Cr 0Cy 0Cg 0Dr 0Dy 0Dg 0Er 0Ey 0Eg 0Fr 0Fy 0Fg 0Gr 0Gy 0Gg
1Ar 1Ay 1Ag 1Br 1By 1Bg 1Cr 1Cy 1Cg 1Dr 1Dy 1Dg 1Er 1Ey 1Eg 1Fr 1Fy 1Fg 1Gr 1Gy 1Gg
X0 X1
2Ar 2Ay 2Ag 2Br 2By 2Bg 2Cr 2Cy 2Cg 2Dr 2Dy 2Dg 2Er 2Ey 2Eg 2Fr 2Fy 2Fg 2Gr 2Gy 2Gg
3Ar 3Ay 3Ag 3Br 3By 3Bg 3Cr 3Cy 3Cg 3Dr 3Dy 3Dg 3Er 3Ey 3Eg 3Fr 3Fy 3Fg 3Gr 3Gy 3Gg
```

This adds up to 86 bits, or 11 bytes total, for a full frame display.

## API

The web server allows users to interact with the clock using a web page. Web page will make HTTP requests to registered API endpoints for performing actions on the clock.

### Data Types

A number of JSON data types are used in the API.

#### LightState

Represents a single light state that can be displayed on the clock face. It consists of a JSON array of integer byte values, 11 in total.

```json
[ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 ]
```

#### LightAnimation

Represents a sequence of [LightState] frames that can be played back at a given frame rate.

```json
{
    "Name": "xxx",
    "FrameRate": 10.0,
    "Frames": [
        LightState,
        LightState,
        LightState
    ]
}
```

#### ProblemDetails

An industry standard JSON structure for descibing a problem enocuntered in an API.

```json
{
    "title": "Problem reason",
    "status": 403,
    "detail": "Short description of the problem",
    "instance": "/API/path/to/entity"
}
```

### POST: /API/lights

Sets the current output state of the lights.

This is a temporary API endpoint for testing/development.

Request body:

```
{
    "outputs": LightState
}
```

#### Responses

##### HTTP 200 Ok

Lights successfully updated.

Response body:
```json
{
    "Status": "OK"
}
```

##### HTTP 400 Bad Request

Request body contains invalid JSON. Server was unable to parse request.

Response body:
```
ProblemDetails
```

##### HTTP 422 Unprocessable Content

Request body was valid JSON, but the JSON failed validation.

Response body:
```
ProblemDetails
```

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.

### GET: /API/animations

Retrieves all available animations.

#### Responses

##### HTTP 200 Ok

Animations listed successfully.

Response body:
```json
[
    LightAnimation,
    LightAnimation,
    LightAnimation,
    ...
]
```

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.

### GET: /API/animations/{name}

Gets the animation definition, identified by the animation name.

#### Responses

##### HTTP 200 Ok

Animation retrieval successful.

Response body:
```
LightAnimation
```

##### HTTP 422 Unprocessable Content

Requested animation name is illegal.

TODO: This is the incorrect HTTP status code for this kind of failure.

Response body:
```
ProblemDetails
```

##### HTTP 404 Not Found

Animation name does not exist.

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.

### POST/PUT: /API/animations

Creates or updates an animation.

Request body:
```
LightAnimation
```

#### Responses

##### HTTP 200 OK

Animation updated successfully.

Response body:
```
LightAnimation
```

##### HTTP 201 Created

Animation created successfully.

Response headers:
```
Location: /API/animations/<name>
```

Response body:
```
LightAnimation
```

##### HTTP 400 Bad Request

Request body contains invalid JSON. Server was unable to parse request.

Response body:
```
ProblemDetails
```

##### HTTP 422 Unprocessable Content

Request body was valid JSON, but the JSON failed validation.

Response body:
```
ProblemDetails
```

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.

### DELETE: /API/animations/{name}

Deletes an existing animation.

#### Responses

##### HTTP 204 No Content

Animation was successfully deleted.

##### HTTP 404 Not Found

Animation name was not found.

##### HTTP 422 Unprocessable Content

Requested animation name is illegal.

TODO: This is the incorrect HTTP status code for this kind of failure.

Response body:
```
ProblemDetails
```

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.

### POST: /API/animations/{name}/run

Starts the named animation on the clock face.

Request body:
None, empty request

#### Responses

##### HTTP 202 Accepted

Animation was found, loaded, and sent to the light driver for playback.

Response body:
None, empry response

##### HTTP 404 Not Found

Animation name was not found, or something other than "run" was supplied.

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.

### POST: /API/playback/custom

Immediately starts playback of a custom animation. Animation name is entirely ignored.

Request body:
```
LightAnimation
```

#### Responses

##### HTTP 204 No Content

Animation playback was successfully started.

##### HTTP 400 Bad Request

Request body contains invalid JSON. Server was unable to parse request.

Response body:
```
ProblemDetails
```

##### HTTP 422 Unprocessable Content

Request body was valid JSON, but the JSON failed validation.

Response body:
```
ProblemDetails
```

##### HTTP 500 Internal Server Error

Something went wrong. Response body contains failure description.
