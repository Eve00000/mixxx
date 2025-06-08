
#include "library/trackset/smarties/dlgsmartiesinfo.h"

#include "moc_dlgsmartiesinfo.cpp"

dlgSmartiesInfo::dlgSmartiesInfo(
        QWidget* parent)
        : QDialog(parent) {
    setupUi(this);
    initializeConditionState(); // Initialize the condition states on UI load
    connect(nextButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onNextButtonClicked);
    connect(previousButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onPreviousButtonClicked);

    // Connect signals to dynamically adjust condition state when fields change
    for (int i = 0; i < 12; ++i) {
        auto* fieldComboBox = findChild<QComboBox*>(QString("comboBoxCondition%1Field").arg(i + 1));
        auto* operatorComboBox = findChild<QComboBox*>(
                QString("comboBoxCondition%1Operator").arg(i + 1));
        auto* valueLineEdit = findChild<QLineEdit*>(QString("lineEditCondition%1Value").arg(i + 1));
        auto* combinerComboBox = findChild<QComboBox*>(
                QString("comboBoxCondition%1Combiner").arg(i + 1));

        if (fieldComboBox && operatorComboBox && valueLineEdit && combinerComboBox) {
            connect(fieldComboBox,
                    &QComboBox::currentTextChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
            connect(operatorComboBox,
                    &QComboBox::currentTextChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
            connect(valueLineEdit,
                    &QLineEdit::textChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
            connect(combinerComboBox,
                    &QComboBox::currentTextChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
        }
    }
}

void dlgSmartiesInfo::init(const QVariantList& smartiesData) {
    qDebug() << "Initializing with data:" << smartiesData;
    populateUI(smartiesData);
}

QVariant dlgSmartiesInfo::getUpdatedData() const {
    // Collect and return the updated data
    return collectUIChanges();
    //    return 1;
}

void dlgSmartiesInfo::populateUI(const QVariantList& smartiesData) {
    if (smartiesData.isEmpty()) {
        return; // No data found for this ID
    }

    if (!smartiesData.isEmpty()) {
        lineEditID->setText(smartiesData[0].toString());
        lineEditName->setText(smartiesData[1].toString());
        spinBoxCount->setValue(smartiesData[2].toInt());
        checkBoxShow->setChecked(smartiesData[3].toBool());
        buttonLock->setText(smartiesData[4].toBool() ? "Unlock" : "Lock");
        checkBoxAutoDJ->setChecked(smartiesData[5].toBool());
        lineEditSearchInput->setText(smartiesData[6].toString());
        lineEditSearchSQL->setText(smartiesData[7].toString());

        QStringList fieldOptions = {"",
                "artist",
                "title",
                "album",
                "album_artist",
                "genre",
                "comment",
                "composer"};
        QStringList operatorOptions = {"",
                "contains",
                "does not contain",
                "is",
                "is not",
                "starts with",
                "ends with",
                "is not empty",
                "is empty"};
        QStringList combinerOptions = {"", ") END", "AND", "OR", ") AND (", ") OR ("};

        int conditionStartIndex = 8; // Adjust based on smartiesData format
        for (int i = 0; i < 12; ++i) {
            int baseIndex = conditionStartIndex + i * 4;

            auto* fieldComboBox = findChild<QComboBox*>(
                    QString("comboBoxCondition%1Field").arg(i + 1));
            if (fieldComboBox) {
                fieldComboBox->clear();
                fieldComboBox->addItems(fieldOptions);
                QString fieldText = smartiesData[baseIndex].isNull()
                        ? ""
                        : smartiesData[baseIndex].toString();
                int index = fieldComboBox->findText(fieldText);
                if (index != -1) {
                    fieldComboBox->setCurrentIndex(index);
                } else if (!fieldText.isEmpty()) {
                    fieldComboBox->insertItem(0, fieldText);
                    fieldComboBox->setCurrentIndex(0);
                }
            }

            auto* operatorComboBox = findChild<QComboBox*>(
                    QString("comboBoxCondition%1Operator").arg(i + 1));
            if (operatorComboBox) {
                operatorComboBox->clear();
                operatorComboBox->addItems(operatorOptions);
                QString operatorText = smartiesData[baseIndex + 1].isNull()
                        ? ""
                        : smartiesData[baseIndex + 1].toString();
                int index = operatorComboBox->findText(operatorText);
                if (index != -1) {
                    operatorComboBox->setCurrentIndex(index);
                } else if (!operatorText.isEmpty()) {
                    operatorComboBox->insertItem(0, operatorText);
                    operatorComboBox->setCurrentIndex(0);
                }
            }

            auto* valueLineEdit = findChild<QLineEdit*>(
                    QString("lineEditCondition%1Value").arg(i + 1));
            if (valueLineEdit) {
                valueLineEdit->setText(smartiesData[baseIndex + 2].isNull()
                                ? ""
                                : smartiesData[baseIndex + 2].toString());
            }

            auto* combinerComboBox = findChild<QComboBox*>(
                    QString("comboBoxCondition%1Combiner").arg(i + 1));
            if (combinerComboBox) {
                combinerComboBox->clear();
                combinerComboBox->addItems(combinerOptions);
                QString combinerText = smartiesData[baseIndex + 3].isNull()
                        ? ""
                        : smartiesData[baseIndex + 3].toString();
                int index = combinerComboBox->findText(combinerText);
                if (index != -1) {
                    combinerComboBox->setCurrentIndex(index);
                } else if (!combinerText.isEmpty()) {
                    combinerComboBox->insertItem(0, combinerText);
                    combinerComboBox->setCurrentIndex(0);
                }
            }
        }
    }
    // Connect the combo box and line edit signals for validation
    connectConditions();

    connect(
            applyButton,
            &QPushButton::clicked,
            this,
            &dlgSmartiesInfo::onApplyButtonClicked);
    connect(
            newButton,
            &QPushButton::clicked,
            this,
            &dlgSmartiesInfo::onNewButtonClicked);
    connect(
            previousButton,
            &QPushButton::clicked,
            this,
            &dlgSmartiesInfo::onPreviousButtonClicked);
    connect(
            nextButton,
            &QPushButton::clicked,
            this,
            &dlgSmartiesInfo::onNextButtonClicked);
    connect(
            okButton,
            &QPushButton::clicked,
            this,
            &dlgSmartiesInfo::onOKButtonClicked);
}

void dlgSmartiesInfo::connectConditions() {
    for (int i = 1; i <= 12; ++i) {
        auto* fieldComboBox = findChild<QComboBox*>(QString("comboBoxCondition%1Field").arg(i));
        auto* operatorComboBox = findChild<QComboBox*>(
                QString("comboBoxCondition%1Operator").arg(i));
        auto* valueLineEdit = findChild<QLineEdit*>(QString("lineEditCondition%1Value").arg(i));
        auto* combinerComboBox = findChild<QComboBox*>(
                QString("comboBoxCondition%1Combiner").arg(i));

        if (fieldComboBox && operatorComboBox && valueLineEdit && combinerComboBox) {
            connect(fieldComboBox,
                    &QComboBox::currentTextChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
            connect(operatorComboBox,
                    &QComboBox::currentTextChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
            connect(combinerComboBox,
                    &QComboBox::currentTextChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
            connect(valueLineEdit,
                    &QLineEdit::textChanged,
                    this,
                    &dlgSmartiesInfo::updateConditionState);
        }
    }
}

QVariantList dlgSmartiesInfo::collectUIChanges() const {
    qDebug() << "CollectUIChanges Started!";
    QVariantList updatedData;
    updatedData.append(lineEditID->text());
    updatedData.append(lineEditName->text());
    updatedData.append(spinBoxCount->value());
    updatedData.append(checkBoxShow->isChecked());
    updatedData.append(buttonLock->text() == "Unlock");
    updatedData.append(checkBoxAutoDJ->isChecked());
    updatedData.append(lineEditSearchInput->text());
    updatedData.append(lineEditSearchSQL->text());

    //    for (int i = 1; i <= 12; ++i) {
    //        updatedData.append(findChild<QComboBox*>(QString("comboBoxCondition%1Field").arg(i))->currentText());
    //        updatedData.append(findChild<QComboBox*>(QString("comboBoxCondition%1Operator").arg(i))->currentText());
    //        updatedData.append(findChild<QLineEdit*>(QString("lineEditCondition%1Value").arg(i))->text());
    //        updatedData.append(findChild<QComboBox*>(QString("comboBoxCondition%1Combiner").arg(i))->currentText());
    //    }

    for (int i = 1; i <= 12; ++i) {
        QString field = findChild<QComboBox*>(
                QString("comboBoxCondition%1Field").arg(i))
                                ->currentText();
        QString op = findChild<QComboBox*>(
                QString("comboBoxCondition%1Operator").arg(i))
                             ->currentText();
        QString value = findChild<QLineEdit*>(QString("lineEditCondition%1Value").arg(i))->text();
        QString combiner = findChild<QComboBox*>(
                QString("comboBoxCondition%1Combiner").arg(i))
                                   ->currentText();

        qDebug() << "Collecting Condition" << i << ":"
                 << "Field:" << field
                 << "Operator:" << op
                 << "Value:" << value
                 << "Combiner:" << combiner;

        updatedData.append(field);
        updatedData.append(op);
        updatedData.append(value);
        updatedData.append(combiner);
    }
    qDebug() << "CollectUIChanges Finished!";
    qDebug() << "Collected data:" << updatedData;
    return updatedData;
}

void dlgSmartiesInfo::onApplyButtonClicked() {
    qDebug() << "Apply button clicked!";
    QVariantList editedData = collectUIChanges();
    qDebug() << "Data collected for Apply:" << editedData;
    emit dataUpdated(editedData); // Emit signal with updated data if needed
    qDebug() << "Data applied without closing the dialog";
    accept();
}

void dlgSmartiesInfo::onNewButtonClicked() {
    // Handle creating a new Smarties entry
    qDebug() << "New button clicked!";
}

void dlgSmartiesInfo::onPreviousButtonClicked() {
    emit requestPreviousSmarties(); // Emit signal to get the previous smarties
    qDebug() << "Previous button clicked, emitted requestPreviousSmarties signal";
}

void dlgSmartiesInfo::onNextButtonClicked() {
    emit requestNextSmarties(); // Emit signal to get the next smarties
    qDebug() << "Next button clicked, emitted requestNextSmarties signal";
}

void dlgSmartiesInfo::onOKButtonClicked() {
    emit dataUpdated(smartiesData); // Emit signal with the current data
    accept();                       // Close the dialog
    qDebug() << "OK button clicked!";
    qDebug() << "Data saved and dialog closed";
}

//  begin sifnal grey out / show next condition
#include <QDebug> // Include this for debugging output

void dlgSmartiesInfo::updateConditionState() {
    // Call this function on any change to re-evaluate enable states based on user interaction
    initializeConditionState();
}

// Ensure initial state on startup by calling updateConditionState once
void dlgSmartiesInfo::initializeConditionState() {
    bool enableNextField = true; // Control to enable the next field in sequence

    for (int i = 0; i < 12; ++i) {
        auto* fieldComboBox = findChild<QComboBox*>(QString("comboBoxCondition%1Field").arg(i + 1));
        auto* operatorComboBox = findChild<QComboBox*>(
                QString("comboBoxCondition%1Operator").arg(i + 1));
        auto* valueLineEdit = findChild<QLineEdit*>(QString("lineEditCondition%1Value").arg(i + 1));
        auto* combinerComboBox = findChild<QComboBox*>(
                QString("comboBoxCondition%1Combiner").arg(i + 1));

        if (fieldComboBox && operatorComboBox && valueLineEdit && combinerComboBox) {
            bool fieldSelected = !fieldComboBox->currentText().isEmpty();
            bool operatorSelected = !operatorComboBox->currentText().isEmpty();
            bool valuePresent = !valueLineEdit->text().isEmpty();
            bool combinerSelected = !combinerComboBox->currentText().isEmpty();

            // Set fieldComboBox enabled if either pre-set or we should enable the next empty one
            //            qDebug() << "fieldComboBox Value i: " << i;
            //            qDebug() << "fieldComboBox Value setEnabled: " << fieldSelected;
            fieldComboBox->setEnabled(fieldSelected || enableNextField);

            // Enable other fields based on field selection and presence of pre-existing values
            operatorComboBox->setEnabled(fieldSelected || operatorSelected);
            valueLineEdit->setEnabled((fieldSelected && operatorSelected) || valuePresent);
            combinerComboBox->setEnabled((fieldSelected && operatorSelected) || combinerSelected);

            // Only enable the next field line if this line has values or has been started
            enableNextField = fieldSelected;
        }
    }
}

// buttonFunctions - handle button-related functionalities

void dlgSmartiesInfo::handleButtonFunctions() {
    // Logic for handling button functionalities can go here.
    // For example:
    connect(applyButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onApplyButtonClicked);
    connect(newButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onNewButtonClicked);
    connect(previousButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onPreviousButtonClicked);
    connect(nextButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onNextButtonClicked);
    connect(okButton, &QPushButton::clicked, this, &dlgSmartiesInfo::onOKButtonClicked);
}
//  end sifnal grey out / show next condition
