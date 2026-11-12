// SPDX-License-Identifier: GPL-3.0-or-later
// A drawn preview of a Mii for the Mii editor: face shape, skin, hair colour, eyes, eyebrows, nose, mouth, facial hair, glasses and
// mole, placed, sized, rotated and coloured from the Mii's own values, so you can see what a change does. Part SHAPES are generic
// (the console draws each eye/mouth/hair style from its own artwork); the part numbers are printed under the face.
#pragma once

#include <QWidget>
#include <map>
#include <string>

class MiiPreview : public QWidget {
    Q_OBJECT
public:
    explicit MiiPreview(QWidget* parent = nullptr);
    /// The Mii's values by field key (mii_db.cpp Fields(): "eye_type", "hair_color", ...).
    void setValues(const std::map<std::string, int>& values);
    QSize sizeHint() const override { return {300, 380}; }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int v(const char* key, int fallback = 0) const;
    std::map<std::string, int> values;
};
