#pragma once

namespace crimson::editor::ui
{
    // Called before every item of the layer is drawn, to put it on the line of its row, if it has one. A row only
    // places the items of the window it was started in, so a tooltip or a popup drawn in the middle of it stays where it is.
    void PlaceItem();

    // Called after a tile is drawn, to continue the line of its grid when the next tile fits
    void ContinueTileLine();
}
