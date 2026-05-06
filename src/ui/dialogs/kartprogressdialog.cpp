#include "kartprogressdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

KartProgressDialog::KartProgressDialog(const QString &titleText, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(titleText);
  setModal(true);

  auto *root = new QVBoxLayout(this);
  m_titleLabel = new QLabel(titleText, this);
  m_titleLabel->setStyleSheet("font-weight: bold;");
  root->addWidget(m_titleLabel);

  m_entryLabel = new QLabel(tr("Preparing..."), this);
  m_entryLabel->setWordWrap(true);
  root->addWidget(m_entryLabel);

  m_bar = new QProgressBar(this);
  m_bar->setRange(0, 1000);
  m_bar->setValue(0);
  root->addWidget(m_bar);

  auto *buttonRow = new QHBoxLayout();
  buttonRow->addStretch(1);
  m_cancelButton = new QPushButton(tr("Cancel"), this);
  buttonRow->addWidget(m_cancelButton);
  root->addLayout(buttonRow);

  connect(m_cancelButton, &QPushButton::clicked, this, [this] {
    m_cancelButton->setEnabled(false);
    m_entryLabel->setText(tr("Cancelling..."));
    emit cancelRequested();
  });

  resize(420, 140);
}

void KartProgressDialog::setFraction(double fraction) {
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;
  m_bar->setValue(static_cast<int>(fraction * 1000.0));
}

void KartProgressDialog::setEntryName(const QString &relPath) {
  m_entryLabel->setText(relPath);
}

void KartProgressDialog::markFinished() {
  m_bar->setValue(1000);
  m_cancelButton->setText(tr("Close"));
  m_cancelButton->setEnabled(true);
  disconnect(m_cancelButton, &QPushButton::clicked, this, nullptr);
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::accept);
}
