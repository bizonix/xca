/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2001 - 2010 Christian Hohnstaedt.
 *
 * All rights reserved.
 */


//#define MDEBUG
#include "MainWindow.h"
#include "ImportMulti.h"

#include <openssl/rand.h>

#include <QtGui/QApplication>
#include <QtGui/QClipboard>
#include <QtGui/QMessageBox>
#include <QtGui/QLabel>
#include <QtGui/QPushButton>
#include <QtGui/QListView>
#include <QtGui/QLineEdit>
#include <QtGui/QTextBrowser>
#include <QtGui/QStatusBar>
#include <QtCore/QList>
#include <QtCore/QTemporaryFile>
#include <QtGui/QInputDialog>

#include "lib/exception.h"
#include "lib/pki_evp.h"
#include "lib/pki_scard.h"
#include "lib/pki_pkcs12.h"
#include "lib/pki_multi.h"
#include "lib/load_obj.h"
#include "lib/pass_info.h"
#include "lib/func.h"
#include "lib/pkcs11.h"
#include "ui_PassRead.h"
#include "ui_PassWrite.h"
#include "ui_About.h"


QPixmap *MainWindow::keyImg = NULL, *MainWindow::csrImg = NULL,
	*MainWindow::certImg = NULL, *MainWindow::tempImg = NULL,
	*MainWindow::nsImg = NULL, *MainWindow::revImg = NULL,
	*MainWindow::appIco = NULL, *MainWindow::scardImg = NULL,
	*MainWindow::doneIco = NULL;

db_key *MainWindow::keys = NULL;
db_x509req *MainWindow::reqs = NULL;
db_x509	*MainWindow::certs = NULL;
db_temp	*MainWindow::temps = NULL;
db_crl	*MainWindow::crls = NULL;

NIDlist *MainWindow::eku_nid = NULL;
NIDlist *MainWindow::dn_nid = NULL;
NIDlist *MainWindow::aia_nid = NULL;

QString MainWindow::mandatory_dn;

static const int x962_curve_nids[] = {
	NID_X9_62_prime192v1,
	NID_X9_62_prime192v2,
	NID_X9_62_prime192v3,
	NID_X9_62_prime239v1,
	NID_X9_62_prime239v2,
	NID_X9_62_prime239v3,
	NID_X9_62_prime256v1,
	NID_X9_62_c2pnb163v1,
	NID_X9_62_c2pnb163v2,
	NID_X9_62_c2pnb163v3,
	NID_X9_62_c2pnb176v1,
	NID_X9_62_c2tnb191v1,
	NID_X9_62_c2tnb191v2,
	NID_X9_62_c2tnb191v3,
	NID_X9_62_c2pnb208w1,
	NID_X9_62_c2tnb239v1,
	NID_X9_62_c2tnb239v2,
	NID_X9_62_c2tnb239v3,
	NID_X9_62_c2pnb272w1,
	NID_X9_62_c2pnb304w1,
	NID_X9_62_c2tnb359v1,
	NID_X9_62_c2pnb368w1,
	NID_X9_62_c2tnb431r1
};

static const int other_curve_nids[] = {
	NID_sect163k1,
	NID_sect163r2,
	NID_sect233k1,
	NID_sect233r1,
	NID_sect283k1,
	NID_sect283r1,
	NID_sect409k1,
	NID_sect409r1,
	NID_sect571k1,
	NID_sect571r1,
	NID_secp224r1,
	NID_secp384r1,
	NID_secp521r1
};
#ifndef OPENSSL_NO_EC
static void init_curves()
{
	pki_evp::num_curves = EC_get_builtin_curves(NULL, 0);
	pki_evp::curves = (EC_builtin_curve*)OPENSSL_malloc(
			(int)(sizeof(EC_builtin_curve) *pki_evp::num_curves));
	if (!pki_evp::curves)
		return;
	EC_get_builtin_curves(pki_evp::curves, pki_evp::num_curves);
	pki_evp::curve_flags = (unsigned char *)OPENSSL_malloc(pki_evp::num_curves);
	if (!pki_evp::curve_flags)
		return;
	for (size_t i=0; i< pki_evp::num_curves; i++) {
		size_t j;

		pki_evp::curve_flags[i] = 0;
		for (j=0; j<ARRAY_SIZE(x962_curve_nids); j++) {
			if (x962_curve_nids[j] == pki_evp::curves[i].nid) {
				pki_evp::curve_flags[i] = CURVE_X962;
				break;
			}
		}
		if (pki_evp::curve_flags[i])
			continue;
		for (j=0; j<ARRAY_SIZE(other_curve_nids); j++) {
			if (other_curve_nids[j] == pki_evp::curves[i].nid) {
				pki_evp::curve_flags[i] = CURVE_OTHER;
				break;
			}
		}
	}
}
#endif
void MainWindow::enableTokenMenu(bool enable)
{
	foreach(QAction* a, scardMenuActions)
		a->setEnabled(enable);
	foreach(QWidget *w, scardList) {
		w->setEnabled(enable);
	}
}

void MainWindow::load_engine()
{
	try {
		pkcs11::load_libs(pkcs11path, false);
	} catch (errorEx &err) {
		Error(err);
	}
	enableTokenMenu(pkcs11::loaded());
}

static QByteArray fileNameEncoderFunc(const QString &fileName)
{
	return filename2bytearray(fileName);
}

static QString fileNAmeDecoderFunc(const QByteArray &localFileName)
{
	return filename2QString(localFileName.constData());
}

MainWindow::MainWindow(QWidget *parent )
	:QMainWindow(parent)
{
	QFile::setEncodingFunction(fileNameEncoderFunc);
	QFile::setDecodingFunction(fileNAmeDecoderFunc);

	dbindex = new QLabel();
	dbindex->setFrameStyle(QFrame::Plain | QFrame::NoFrame);
	dbindex->setMargin(6);

	statusBar()->addWidget(dbindex, 1);
	mandatory_dn = "";
	string_opt = "MASK:0x2002";

	setupUi(this);
	setWindowTitle(tr(XCA_TITLE));

	wdList << keyButtons << reqButtons << certButtons <<
		tempButtons <<	crlButtons;
	init_menu();
	setItemEnabled(false);

	init_images();
	homedir = getHomeDir();
#ifndef OPENSSL_NO_EC
	init_curves();
#endif
#ifdef MDEBUG
	CRYPTO_malloc_debug_init();
	CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);
	fprintf(stderr, "malloc() debugging on.\n");
#endif

	ERR_load_crypto_strings();
	OpenSSL_add_all_algorithms();

	/* read in all our own OIDs */
	initOIDs();

	eku_nid = read_nidlist("eku.txt");
	dn_nid = read_nidlist("dn.txt");
	aia_nid = read_nidlist("aia.txt");
	setAcceptDrops(true);
}

void MainWindow::dropEvent(QDropEvent *event)
{
	QList<QUrl> urls = event->mimeData()->urls();
	QUrl u;
	ImportMulti *dlgi = new ImportMulti(this);
	QStringList failed;

	foreach(u, urls) {
		QString s = u.toLocalFile();
	        pki_multi *pki = probeAnything(s);
		if (pki && !pki->count())
			failed << s;
	        dlgi->addItem(pki);
	}
	dlgi->execute(1, failed);
	delete dlgi;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void MainWindow::setItemEnabled(bool enable)
{
	foreach(QWidget *w, wdList) {
		w->setEnabled(enable);
	}
	foreach(QAction *a, acList) {
		a->setEnabled(enable);
	}
	enableTokenMenu(pkcs11::loaded());
}

/* creates a new nid list from the given filename */
NIDlist *MainWindow::read_nidlist(QString name)
{
	NIDlist nl;
	name = QDir::separator() + name;

#ifndef WIN32
	/* first try $HOME/xca/ */
	nl = readNIDlist(getUserSettingsDir() + QDir::separator() + name);
#if !defined(Q_WS_MAC)
	if (nl.count() == 0){
		/* next is /etx/xca/... */
		nl = readNIDlist(QString(ETC) + name);
	}
#endif
#endif
	if (nl.count() == 0) {
		/* look at /usr/(local/)share/xca/ */
		nl = readNIDlist(getPrefix() + name);
	}
	return new NIDlist(nl);
}

void MainWindow::init_images()
{
	keyImg = loadImg("bigkey.png");
	csrImg = loadImg("bigcsr.png");
	certImg = loadImg("bigcert.png");
	tempImg = loadImg("bigtemp.png");
	nsImg = loadImg("netscape.png");
	revImg = loadImg("bigcrl.png");
	scardImg = loadImg("bigscard.png");
	appIco = loadImg("key.xpm");
	doneIco = loadImg("done.png");
	bigKey->setPixmap(*keyImg);
	bigCsr->setPixmap(*csrImg);
	bigCert->setPixmap(*certImg);
	bigTemp->setPixmap(*tempImg);
	bigRev->setPixmap(*revImg);
	setWindowIcon(*appIco);
	pki_evp::icon[0] = loadImg("key.png");
	pki_evp::icon[1] = loadImg("halfkey.png");
	pki_scard::icon[0] = loadImg("scard.png");
	pki_x509req::icon[0] = loadImg("req.png");
	pki_x509req::icon[1] = loadImg("reqkey.png");
	pki_x509req::icon[2] = loadImg("spki.png");
	pki_x509req::icon[3] = doneIco;
	pki_x509::icon[0] = loadImg("validcert.png");
	pki_x509::icon[1] = loadImg("validcertkey.png");
	pki_x509::icon[2] = loadImg("invalidcert.png");
	pki_x509::icon[3] = loadImg("invalidcertkey.png");
	pki_x509::icon[4] = loadImg("revoked.png");
	pki_x509::icon[5] = doneIco;
	pki_temp::icon = loadImg("template.png");
	pki_crl::icon = loadImg("crl.png");
}

void MainWindow::read_cmdline()
{
	int cnt = 1, opt = 0, force_load = 0;
	char *arg = NULL;
	exitApp = 0;
	QStringList failed;
	ImportMulti *dlgi = new ImportMulti(this);
	while (cnt < qApp->argc()) {
		arg = qApp->argv()[cnt];
		if (arg[0] == '-') { // option
			opt = 1;
			switch (arg[1]) {
				case 'c':
				case 'r':
				case 'k':
				case 'p':
				case '7':
				case 'l':
				case 't':
				case 'P':
					break;
				case 'd':
					force_load=1;
					break;
				case 'v':
					cmd_version();
					opt=0;
					break;
				case 'x':
					exitApp = 1;
					opt=0;
					break;
				case 'h':
					cmd_help(NULL);
					opt=0;
					break;
				default:
					 cmd_help(CCHAR(tr("no such option: %1").arg(arg)));
			}
			if (arg[2] != '\0' && opt==1) {
				 arg+=2;
			} else {
				cnt++;
				continue;
			}
		}
		QString file = filename2QString(arg);
		if (force_load) {
			changeDB(file);
			force_load = 0;
		} else {
			pki_multi *pki = probeAnything(file);
			if (pki && !pki->count())
				failed << file;
			dlgi->addItem(pki);
		}
		cnt++;
	}
	dlgi->execute(1, failed); /* force showing of import dialog */
	if (dlgi->result() == QDialog::Rejected)
		exitApp = 1;
	delete dlgi;
}

void MainWindow::loadPem()
{
	load_pem l;
	if (keys)
		keys->load_default(l);
}

void MainWindow::pastePem()
{
	Ui::About ui;
	QDialog *input = new QDialog(this, 0);

	ui.setupUi(input);
	delete ui.textbox;
	QTextEdit *textbox = new QTextEdit(input);
	ui.vboxLayout->addWidget(textbox);
	ui.button->setText(tr("Import PEM data"));
	input->setWindowTitle(tr(XCA_TITLE));
	if (input->exec()) {
		QString txt = textbox->toPlainText();
		QTemporaryFile f;
		f.open();
		f.write(textbox->toPlainText().toAscii());
		f.flush();
		pki_multi *pem = NULL;
		ImportMulti *dlgi = NULL;
		try {
			pem = new pki_multi();
			dlgi = new ImportMulti(this);
			pem->fload(f.fileName());
			dlgi->addItem(pem);
			dlgi->execute(1);
		}
		catch (errorEx &err) {
			Error(err);
		}
		delete dlgi;
	}
	delete input;
}

void MainWindow::initToken()
{
	bool ok;
	if (!pkcs11::loaded())
		return;
	try {
		pkcs11 p11;
		slotid slot;
		char pin[MAX_PASS_LENGTH];
		int pinlen;

		if (!p11.selectToken(&slot, this))
			return;

		tkInfo ti = p11.tokenInfo(slot);
		QString slotname = QString("%1 (#%2)").
			arg(ti.label()).arg(ti.serial());

		pass_info p(XCA_TITLE,
			tr("Please enter the original SO PIN (PUK) of the token '%1'").
			arg(slotname) + "\n" + ti.pinInfo());
		p.setPin();
		if (ti.tokenInitialized()) {
			pinlen = passRead(pin, MAX_PASS_LENGTH, 0, &p);
		} else {
			p.setDescription(tr("Please enter the new SO PIN (PUK) of the token '%1'").
			arg(slotname) + "\n" + ti.pinInfo());
			pinlen = passWrite(pin, MAX_PASS_LENGTH, 0, &p);
		}
		if (pinlen < 0)
			return;
		QString label = QInputDialog::getText(this, XCA_TITLE,
			tr("The new label of the token '%1'").
			arg(slotname), QLineEdit::Normal, QString(), &ok);
		if (!ok)
			return;
		p11.initToken(slot, (unsigned char*)pin, pinlen, label);
	} catch (errorEx &err) {
		Error(err);
        }
}

void MainWindow::changePin(bool so)
{
	if (!pkcs11::loaded())
		return;
	try {
		pkcs11 p11;
		slotid slot;

		if (!p11.selectToken(&slot, this))
			return;
		p11.changePin(slot, so);
	} catch (errorEx &err) {
		Error(err);
        }
}

void MainWindow::changeSoPin()
{
	changePin(true);
}

void MainWindow::initPin()
{
	if (!pkcs11::loaded())
		return;
	try {
		pkcs11 p11;
		slotid slot;

		if (!p11.selectToken(&slot, this))
			return;
		p11.initPin(slot);
	} catch (errorEx &err) {
		Error(err);
        }
}


void MainWindow::manageToken()
{
	pkcs11 p11;
	slotid slot;
	pki_scard *card = NULL;
	pki_x509 *cert = NULL;
	ImportMulti *dlgi = NULL;

	if (!pkcs11::loaded())
		return;

	try {
		if (!p11.selectToken(&slot, this))
			return;

		ImportMulti *dlgi = new ImportMulti(this);

		dlgi->tokenInfo(slot);
		QList<CK_OBJECT_HANDLE> objects;

		QList<CK_MECHANISM_TYPE> ml = p11.mechanismList(slot);
		if (ml.count() == 0)
			ml << CKM_SHA1_RSA_PKCS;
		pk11_attlist atts(pk11_attr_ulong(CKA_CLASS,
				CKO_PUBLIC_KEY));

		p11.startSession(slot);
		objects = p11.objectList(atts);

		for (int j=0; j< objects.count(); j++) {
			card = new pki_scard("");
			try {
				card->load_token(p11, objects[j]);
				card->setMech_list(ml);
				dlgi->addItem(card);
			} catch (errorEx &err) {
				Error(err);
				delete card;
			}
			card = NULL;
		}
		atts.reset();
		atts << pk11_attr_ulong(CKA_CLASS, CKO_CERTIFICATE) <<
			pk11_attr_ulong(CKA_CERTIFICATE_TYPE,CKC_X_509);
		objects = p11.objectList(atts);

		for (int j=0; j< objects.count(); j++) {
			cert = new pki_x509("");
			try {
				cert->load_token(p11, objects[j]);
				cert->setTrust(2);
				dlgi->addItem(cert);
			} catch (errorEx &err) {
				Error(err);
				delete cert;
			}
			cert = NULL;
		}
		if (dlgi->entries() == 0) {
			tkInfo ti = p11.tokenInfo();
			QMessageBox::information(this, XCA_TITLE,
				tr("The token '%1' did not contain any keys or certificates").
                                arg(ti.label()));
		} else {
			dlgi->execute(true);
		}
	} catch (errorEx &err) {
		Error(err);
        }
	if (card)
		delete card;
	if (cert)
		delete cert;
	if (dlgi)
		delete dlgi;
}

MainWindow::~MainWindow()
{
	close_database();
	ERR_free_strings();
	EVP_cleanup();
	OBJ_cleanup();
	if (eku_nid)
		delete eku_nid;
	if (dn_nid)
		delete dn_nid;
	if (aia_nid)
		delete aia_nid;
	delete dbindex;
#ifdef MDEBUG
	fprintf(stderr, "Memdebug:\n");
	CRYPTO_mem_leaks_fp(stderr);
#endif
}

QString makeSalt(void)
{
	unsigned char rand[2];
	char saltbuf[10];

	RAND_bytes(rand, 2);
	snprintf(saltbuf, 10, "S%02X%02X", rand[0], rand[1]);
	return QString(saltbuf);
}

void MainWindow::changeDbPass()
{

	char pass[MAX_PASS_LENGTH] = { 0, };
	int keylen;

	pass_info p(tr("New Password"), tr("Please enter the new password "
			"to encrypt your private keys in the database-file"),
			this);

	keylen = passWrite(pass, MAX_PASS_LENGTH-1, 0, &p);
	if (keylen < 0)
		return;
	pass[keylen] = '\0';
	QString tempn = dbfile + "{new}";
	try {
		if (!QFile::copy(dbfile, tempn))
			throw errorEx("Could not create temporary file: " +
				tempn);
		QString passhash = updateDbPassword(tempn, pass);

		QFile new_file(tempn);
		db mydb(dbfile);
		mydb.mv(new_file);
		close_database();
		pki_evp::passHash = passhash;
		strncpy(pki_evp::passwd, pass, MAX_PASS_LENGTH);
		init_database();
	} catch (errorEx &ex) {
		QFile::remove(tempn);
		Error(ex);
	}
}

QString MainWindow::updateDbPassword(QString newdb, char *pass)
{
	db mydb(newdb);

	QString salt = makeSalt();
	QString passhash = pki_evp::sha512passwd(pass, salt);
	mydb.set((const unsigned char *)CCHAR(passhash),
		passhash.length()+1, 1, setting, "pwhash");

	QList<pki_evp*> klist;
	mydb.first();
	while (mydb.find(asym_key, QString()) == 0) {
		QString s;
		pki_evp *key;
		unsigned char *p;
		db_header_t head;

		p = mydb.load(&head);
		if (!p) {
			printf("Load was empty !\n");
			goto next;
		}
		key = new pki_evp();
		if (key->getVersion() < head.version) {
			printf("Item[%s]: Version %d "
				"> known version: %d -> ignored\n",
				head.name, head.version,
				key->getVersion()
			);
			free(p);
			delete key;
			goto next;
		}
		key->setIntName(QString::fromUtf8(head.name));

		try {
			key->fromData(p, &head);
		}
		catch (errorEx &err) {
			err.appendString(key->getIntName());
			Error(err);
			delete key;
			key = NULL;
		}
		free(p);
		if (key && key->getOwnPass() == pki_key::ptCommon &&
			!key->isPubKey())
		{
			EVP_PKEY *evp = key->decryptKey();
			key->set_evp_key(evp);
			key->encryptKey(pass);
			klist << key;
		} else if (key)
			delete key;
next:
		if (mydb.next())
			break;
	}
	for (int i=0; i< klist.count(); i++) {
		pki_evp *key = klist[i];
		QByteArray ba = key->toData();
		mydb.set((const unsigned char*)ba.constData(), ba.count(),
			key->getVersion(), key->getType(), key->getIntName());
		delete key;
	}
	return passhash;
}

int MainWindow::initPass()
{
	db mydb(dbfile);
	char *pass;
	pki_evp::passHash = QString();
	QString salt;

	pass_info p(tr("New Password"), tr("Please enter a password, "
			"that will be used to encrypt your private keys "
			"in the database file: '%1'").arg(dbfile), this);
	if (!mydb.find(setting, "pwhash")) {
		if ((pass = (char *)mydb.load(NULL))) {
			pki_evp::passHash = pass;
			free(pass);
		}
	}
	if (pki_evp::passHash.isEmpty()) {
		int keylen = passWrite((char *)pki_evp::passwd,
				MAX_PASS_LENGTH-1, 0, &p);
		if (keylen < 0)
			return 0;
		pki_evp::passwd[keylen]='\0';
		salt = makeSalt();
		pki_evp::passHash = pki_evp::sha512passwd(pki_evp::passwd,salt);
		mydb.set((const unsigned char *)CCHAR(pki_evp::passHash),
			pki_evp::passHash.length()+1, 1, setting, "pwhash");
	} else {
		int keylen=0;
		while (pki_evp::sha512passwd(pki_evp::passwd, pki_evp::passHash)
				!= pki_evp::passHash)
		{
			if (keylen !=0) QMessageBox::warning(this,tr(XCA_TITLE),
				tr("Password verify error, please try again"));
			p.setTitle(tr("Password"));
			p.setDescription(tr("Please enter the password for unlocking the database: '%1'").arg(dbfile));
			keylen = passRead(pki_evp::passwd, MAX_PASS_LENGTH-1, 0, &p);
			if (keylen < 0)
				return 1;
			pki_evp::passwd[keylen]='\0';
			if (pki_evp::passHash.left(1) == "S")
				continue;
			/* Start automatic update from md5 to salted sha512
			 * if the password is correct. my md5 hash does not
			 * start with 'S', while my new hash does. */
			if (pki_evp::md5passwd(pki_evp::passwd) ==
						pki_evp::passHash )
			{
				salt = makeSalt();
				pki_evp::passHash = pki_evp::sha512passwd(
						pki_evp::passwd, salt);
				mydb.set((const unsigned char *)CCHAR(
					pki_evp::passHash),
					pki_evp::passHash.length() +1, 1,
					setting, "pwhash");
			}
		}
	}
	return 1;
}

static int hex2bin(QString &x, char *buf, int buflen)
{
	int len = x.length();
	bool ok = false;
	if (len % 2)
		return -1;
	len /= 2;
	if (len > buflen)
		return -1;

	for (int i=0; i<len; i++) {
		buf[i] = x.mid(i*2, 2).toInt(&ok, 16);
		if (!ok)
			return -1;
	}
	return len;
}

static const QString hexwarn = MainWindow::tr("Hex password must only contain the characters '0' - '9' and 'a' - 'f' and it must consist of an even number of characters");
// Static Password Callback functions
int MainWindow::passRead(char *buf, int size, int, void *userdata)
{
	int ret = -1;
	pass_info *p = (pass_info *)userdata;
	Ui::PassRead ui;
	QDialog *dlg = new QDialog(p->getWidget());
	ui.setupUi(dlg);
	if (p != NULL) {
		ui.image->setPixmap(p->getImage());
		ui.description->setText(p->getDescription());
		ui.title->setText(p->getType());
		ui.label->setText(p->getType());
		dlg->setWindowTitle(p->getTitle());
		if (p->getType() != "PIN")
			ui.takeHex->hide();
	}

	while (dlg->exec()) {
		QString x = ui.pass->text();
		if (ui.takeHex->isChecked()) {
			ret = hex2bin(x, buf, size);
			if (ret != -1)
				break;
		} else {
			strncpy(buf, x.toAscii(), size);
			ret = x.length();
			break;
		}
		QMessageBox::warning(p->getWidget(), XCA_TITLE, hexwarn);
	}
	delete dlg;
	return ret;
}

int MainWindow::passWrite(char *buf, int size, int, void *userdata)
{
	int ret = -1;
	pass_info *p = (pass_info *)userdata;
	Ui::PassWrite ui;
	QDialog *dlg = new QDialog(p->getWidget());
	ui.setupUi(dlg);
	if (p != NULL) {
		ui.image->setPixmap(p->getImage()) ;
		ui.description->setText(p->getDescription());
		ui.title->setText(p->getType());
		ui.label->setText(p->getType());
		ui.repeatLabel->setText(tr("Repeat %1").arg(p->getType()));
		dlg->setWindowTitle(p->getTitle());
		if (p->getType() != "PIN")
			ui.takeHex->hide();
	}
	dlg->show();
	dlg->activateWindow();
	ui.passA->setFocus();

	while (dlg->exec()) {
		QString A = ui.passA->text();
		QString B = ui.passB->text();
		if (A == B) {
			if (ui.takeHex->isChecked()) {
				ret = hex2bin(A, buf, size);
				if (ret != -1)
					break;
			} else {
				strncpy(buf, A.toAscii(), size);
				ret = A.length();
				break;
			}
			QMessageBox::warning(p->getWidget(), XCA_TITLE,
						hexwarn);
		} else {
			QMessageBox::warning(p->getWidget(), XCA_TITLE,
					tr("%1 missmatch").arg(p->getType()));
		}
	}
	delete dlg;
	return ret;
}

void MainWindow::Error(errorEx &err)
{
	if (err.isEmpty())
		 return;
	QString msg =  tr("The following error occured:") + "\n" + err.getString();
	QMessageBox box(QMessageBox::Warning, XCA_TITLE,
		msg, QMessageBox::Ok, NULL);
	box.addButton(QMessageBox::Apply)->setText(tr("Copy to Clipboard"));
	if (box.exec() == QMessageBox::Apply) {
		QClipboard *cb = QApplication::clipboard();
		cb->setText(msg);
	}
}

QString MainWindow::getPath()
{
	return workingdir;
}

void MainWindow::setPath(QString str)
{
	db mydb(dbfile);
	workingdir = str;
	mydb.set((const unsigned char *)CCHAR(str), str.length()+1, 1, setting, "workingdir");
}

void MainWindow::connNewX509(NewX509 *nx)
{
	connect( nx, SIGNAL(genKey(QString)), keys, SLOT(newItem(QString)) );
	connect( keys, SIGNAL(keyDone(QString)), nx, SLOT(newKeyDone(QString)) );
	connect( nx, SIGNAL(showReq(QString)), reqs, SLOT(showItem(QString)));
}

void MainWindow::importAnything(QString file)
{
	ImportMulti *dlgi = new ImportMulti(this);
	QStringList failed;
	pki_multi *pki = probeAnything(file);
	if (pki && !pki->count())
		failed << file;
	dlgi->addItem(pki);
	dlgi->execute(1, failed);
	delete dlgi;
}

pki_multi *MainWindow::probeAnything(QString file)
{
	pki_multi *pki = new pki_multi();

	try {
		if (file.endsWith(".xdb")) {
			try {
				db mydb(file);
				mydb.verify_magic();
				changeDB(file);
				delete pki;
				return NULL;
			} catch (errorEx &err) {
			}
		}
		pki->probeAnything(file);
	} catch (errorEx &err) {
		Error(err);
	}
	return pki;
}

void MainWindow::generateDHparam()
{
	DH *dh = NULL;
	FILE *fp = NULL;
	QProgressBar *bar = NULL;
	bool ok;
	int num = QInputDialog::getDouble(this, XCA_TITLE, tr("DH parameter bits"),
		1024, 1024, 4096, 0, &ok);

	if (!ok)
		return;
	try {
		QStatusBar *status = statusBar();
		bar = new QProgressBar();
		check_oom(bar);
		bar->setMinimum(0);
		bar->setMaximum(100);
		status->addPermanentWidget(bar, 1);
		dh = DH_generate_parameters(num, 2, inc_progress_bar, bar);
		status->removeWidget(bar);
		openssl_error();

		QString fname = QString("%1/dh%2.pem").arg(homedir).arg(num);
		fname = QFileDialog::getSaveFileName(this, QString(),
			fname, "All files ( * )", NULL);
		if (fname == "")
			throw errorEx("");
		fp = fopen(QString2filename(fname), "w");
		if (fp == NULL) {
			throw errorEx(tr("Error opening file: '%1': %2").
				arg(fname).arg(strerror(errno)));
		}
		PEM_write_DHparams(fp, dh);
		openssl_error();
	} catch (errorEx &err) {
		Error(err);
	}
	if (dh)
		DH_free(dh);
	if (fp)
		fclose(fp);
	if (bar)
		delete bar;
}

